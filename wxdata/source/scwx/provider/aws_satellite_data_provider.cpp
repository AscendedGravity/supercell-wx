#include <scwx/provider/aws_satellite_data_provider.hpp>
#include <scwx/util/environment.hpp>
#include <scwx/util/logger.hpp>
#include <scwx/util/time.hpp>

#include <atomic>
#include <shared_mutex>
#include <map>
#include <list>
#include <algorithm>
#include <sstream>

#include <aws/core/auth/AWSCredentials.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/ListObjectsV2Request.h>
#include <fmt/format.h>

#if (__cpp_lib_chrono < 201907L)
#   include <date/date.h>
#endif

namespace scwx::provider
{

static const std::string logPrefix_ =
   "scwx::provider::aws_satellite_data_provider";
static const auto logger_ = util::Logger::Create(logPrefix_);

static const std::string kSatelliteBucketName_ = "noaa-goes19";
static const std::string kSatelliteRegion_     = "us-east-1";

class AwsSatelliteDataProvider::Impl
{
public:
   struct ObjectRecord
   {
      explicit ObjectRecord(
         const std::string&                    key,
         std::chrono::system_clock::time_point lastModified) :
          key_ {key}, lastModified_ {lastModified}
      {
      }

      std::string                           key_;
      std::chrono::system_clock::time_point lastModified_;
   };

   explicit Impl(common::SatelliteBand band) :
       band_ {band},
       bandName_ {common::GetSatelliteBandName(band)},
       client_ {nullptr},
       objects_ {},
       objectsMutex_ {},
       objectDates_ {}
   {
      // Disable EC2 metadata fetch
      util::SetEnvironment("AWS_EC2_METADATA_DISABLED", "true");

      // Anonymous credentials for public NOAA buckets
      Aws::Auth::AWSCredentials credentials {};

      Aws::Client::ClientConfiguration config;
      config.region           = kSatelliteRegion_;
      config.connectTimeoutMs = 10000;

      client_ = std::make_shared<Aws::S3::S3Client>(
         credentials,
         Aws::MakeShared<Aws::S3::S3EndpointProvider>(
            Aws::S3::S3Client::GetAllocationTag()),
         config);
   }

   ~Impl() { running_ = false; }

   common::SatelliteBand band_;
   std::string           bandName_;

   std::shared_ptr<Aws::S3::S3Client> client_;

   std::map<std::chrono::system_clock::time_point, ObjectRecord> objects_;
   std::shared_mutex                                             objectsMutex_;
   std::list<std::chrono::system_clock::time_point>              objectDates_;

   std::atomic<bool> running_ {true};
};

AwsSatelliteDataProvider::AwsSatelliteDataProvider(common::SatelliteBand band) :
    p(std::make_unique<Impl>(band))
{
}

AwsSatelliteDataProvider::~AwsSatelliteDataProvider() = default;

AwsSatelliteDataProvider::AwsSatelliteDataProvider(
   AwsSatelliteDataProvider&&) noexcept = default;
AwsSatelliteDataProvider& AwsSatelliteDataProvider::operator=(
   AwsSatelliteDataProvider&&) noexcept = default;

std::tuple<bool, size_t, size_t> AwsSatelliteDataProvider::ListObjects(
   std::chrono::system_clock::time_point date)
{
   using namespace std::chrono;
#if (__cpp_lib_chrono >= 201907L)
   using sys_days = std::chrono::sys_days;
   using namespace std::chrono;
#else
   using sys_days = date::sys_days;
   using namespace date;
#endif

   auto     day         = floor<days>(date);
   auto     ymd         = year_month_day {day};
   int      year        = (int) ymd.year();
   sys_days startOfYear = sys_days {ymd.year() / January / 1};
   int      doy         = (duration_cast<days>(day - startOfYear).count()) + 1;

   // Prefix for the entire day folder: ABI-L2-CMIPC/YYYY/JJJ/
   std::string prefix = fmt::format("ABI-L2-CMIPC/{:04d}/{:03d}/", year, doy);

   logger_->debug("ListObjects: bucket={}, prefix={}, band={}",
                  kSatelliteBucketName_,
                  prefix,
                  p->bandName_);

   Aws::S3::Model::ListObjectsV2Request request;
   request.SetBucket(kSatelliteBucketName_);
   request.SetPrefix(prefix);

   auto outcome = p->client_->ListObjectsV2(request);

   size_t newObjects   = 0;
   size_t totalObjects = 0;

   if (outcome.IsSuccess())
   {
      auto& objects = outcome.GetResult().GetContents();
      logger_->debug("Found {} total objects for prefix", objects.size());

      // Format to filter for: -M<ScanMode>C<BandName>_
      // Filename format: OR_ABI-L2-CMIPC-M6C13_G19_s...
      std::string channelFilter = fmt::format("C{:02d}", (int) p->band_ + 1);

      std::unique_lock lock(p->objectsMutex_);

      std::for_each(
         objects.cbegin(),
         objects.cend(),
         [&](const Aws::S3::Model::Object& object)
         {
            std::string key = object.GetKey();

            // Filter for files corresponding to our selected band
            if (key.find(channelFilter) != std::string::npos &&
                key.ends_with(".nc"))
            {
               auto time = GetTimePointFromKey(key);
               if (time.time_since_epoch().count() > 0)
               {
                  std::chrono::seconds lastModifiedSeconds {
                     object.GetLastModified().Seconds()};
                  std::chrono::system_clock::time_point lastModified {
                     lastModifiedSeconds};

                  auto [it, inserted] = p->objects_.insert_or_assign(
                     time, Impl::ObjectRecord {key, lastModified});

                  if (inserted)
                  {
                     newObjects++;
                  }
                  totalObjects++;
               }
            }
         });

      // Update date cache
      p->objectDates_.remove(day);
      p->objectDates_.push_back(day);

      logger_->debug("ListObjects completed: {} new, {} total for band {}",
                     newObjects,
                     totalObjects,
                     p->bandName_);
   }
   else
   {
      logger_->warn("Could not list objects: {}",
                    outcome.GetError().GetMessage());
   }

   return {outcome.IsSuccess(), newObjects, totalObjects};
}

std::vector<std::chrono::system_clock::time_point>
AwsSatelliteDataProvider::GetTimePointsByDate(
   std::chrono::system_clock::time_point date)
{
   using namespace std::chrono;
   auto day = floor<days>(date);

   std::vector<std::chrono::system_clock::time_point> timePoints {};

   std::shared_lock lock(p->objectsMutex_);

   const auto objectsBegin = p->objects_.lower_bound(day);
   const auto objectsEnd   = p->objects_.lower_bound(day + days {1});

   std::transform(objectsBegin,
                  objectsEnd,
                  std::back_inserter(timePoints),
                  [](const auto& object) { return object.first; });

   return timePoints;
}

std::string AwsSatelliteDataProvider::FindLatestKey()
{
   std::string      key {};
   std::shared_lock lock(p->objectsMutex_);
   if (!p->objects_.empty())
   {
      key = p->objects_.crbegin()->second.key_;
   }
   return key;
}

std::chrono::system_clock::time_point AwsSatelliteDataProvider::FindLatestTime()
{
   std::chrono::system_clock::time_point time {};
   std::shared_lock                      lock(p->objectsMutex_);
   if (!p->objects_.empty())
   {
      time = p->objects_.crbegin()->first;
   }
   return time;
}

std::string AwsSatelliteDataProvider::DownloadObject(const std::string& key)
{
   logger_->debug(
      "Downloading S3 object: bucket={}, key={}", kSatelliteBucketName_, key);

   Aws::S3::Model::GetObjectRequest request;
   request.SetBucket(kSatelliteBucketName_);
   request.SetKey(key);

   request.SetContinueRequestHandler([this](const Aws::Http::HttpRequest*)
                                     { return p->running_.load(); });

   auto outcome = p->client_->GetObject(request);

   if (outcome.IsSuccess())
   {
      auto&             body = outcome.GetResultWithOwnership().GetBody();
      std::stringstream ss;
      ss << body.rdbuf();
      logger_->debug("Download completed for key: {}", key);
      return ss.str();
   }
   else if (p->running_)
   {
      logger_->warn("Could not download object: {}",
                    outcome.GetError().GetMessage());
   }

   return {};
}

void AwsSatelliteDataProvider::Shutdown() noexcept
{
   p->running_ = false;
}

std::chrono::system_clock::time_point
AwsSatelliteDataProvider::GetTimePointFromKey(const std::string& key)
{
   std::chrono::system_clock::time_point time {};

   // Filename format contains _sYYYYJJJHHMMSS3
   size_t startPos = key.find("_s");
   if (startPos != std::string::npos && key.size() >= startPos + 16)
   {
      std::string timeStr = key.substr(startPos + 2, 14);
      try
      {
         int y      = std::stoi(timeStr.substr(0, 4));
         int doy    = std::stoi(timeStr.substr(4, 3));
         int hour   = std::stoi(timeStr.substr(7, 2));
         int minute = std::stoi(timeStr.substr(9, 2));
         int second = std::stoi(timeStr.substr(11, 2));

         using namespace std::chrono;
#if (__cpp_lib_chrono >= 201907L)
         using sys_days = std::chrono::sys_days;
         sys_days startOfYear =
            sys_days {std::chrono::year {y} / std::chrono::January / 1};
#else
         using sys_days       = date::sys_days;
         sys_days startOfYear = sys_days {date::year {y} / date::January / 1};
#endif

         time = startOfYear + days {doy - 1} + hours {hour} + minutes {minute} +
                seconds {second};
      }
      catch (...)
      {
         logger_->warn("Time not parsable from key: \"{}\"", key);
      }
   }
   return time;
}

} // namespace scwx::provider
