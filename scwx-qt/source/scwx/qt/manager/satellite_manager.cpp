#include <scwx/qt/manager/satellite_manager.hpp>
#include <scwx/provider/aws_satellite_data_provider.hpp>
#include <scwx/util/satellite_reader.hpp>
#include <scwx/util/logger.hpp>

#include <scwx/util/map.hpp>

#include <atomic>
#include <list>
#include <mutex>
#include <set>
#include <optional>
#include <shared_mutex>

#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/system/errc.hpp>

namespace scwx
{
namespace qt
{
namespace manager
{

static const std::string logPrefix_ = "scwx::qt::manager::satellite_manager";
static const auto        logger_    = scwx::util::Logger::Create(logPrefix_);

class SatelliteManager::Impl
{
public:
   explicit Impl(SatelliteManager* self) :
       self_ {self},
       band_ {common::SatelliteBand::Unknown},
       provider_ {common::SatelliteBand::Band01},
       threadPool_ {1u},
       refreshTimer_ {threadPool_},
       refreshEnabled_ {false}
   {
   }

   ~Impl()
   {
      {
         std::unique_lock lock(timerMutex_);
         refreshTimer_.cancel();
      }
      provider_.Shutdown();
      threadPool_.join();
   }

   void SelectBand(common::SatelliteBand band);
   void FetchData();
   void EnableRefresh(bool enabled);
   void RefreshNow();
   void LoadDataForTime(common::SatelliteBand                 band,
                        std::chrono::system_clock::time_point time);
   void FetchDataForTime();
   std::chrono::system_clock::time_point
        GetAdjacentSceneTime(std::chrono::system_clock::time_point time,
                             bool                                  previous) const;
   void ScheduleRefresh();
   void CancelRefresh();

   void ClearSceneCache();
   void PruneSceneCache();
   void CacheScene(std::chrono::system_clock::time_point sceneTime,
                   const std::vector<float>&             vertices,
                   const std::vector<std::uint8_t>&      moments);

   SatelliteManager* self_;

   common::SatelliteBand              band_;
   provider::AwsSatelliteDataProvider provider_;

   boost::asio::thread_pool  threadPool_;
   boost::asio::steady_timer refreshTimer_;
   std::mutex                timerMutex_;
   std::atomic<bool>         refreshEnabled_ {false};

   std::vector<float>                    vertices_ {};
   std::vector<std::uint8_t>             moments_ {};
   std::chrono::system_clock::time_point sweepTime_ {};
   mutable std::mutex                    dataMutex_ {};

   static constexpr std::chrono::milliseconds kFastRetryInterval_ {15000};
   static constexpr std::chrono::milliseconds kSlowRetryInterval_ {120000};

   std::optional<std::chrono::system_clock::time_point> requestedTime_ {};

   // Set of all known scene observation times for the active band
   std::set<std::chrono::system_clock::time_point> availableTimes_ {};

   struct CachedScene
   {
      std::vector<float>        vertices;
      std::vector<std::uint8_t> moments;
   };

   std::map<std::chrono::system_clock::time_point, CachedScene> sceneCache_ {};
   std::list<std::chrono::system_clock::time_point> sceneCacheOrder_ {};

   static constexpr std::size_t kMaxCachedScenes_ {50};
};

void SatelliteManager::Impl::SelectBand(common::SatelliteBand band)
{
   // Cancel any pending refresh timer
   CancelRefresh();

   // Defer band switch and data fetch to the thread pool so that
   // any in-progress provider operation completes first, avoiding
   // use-after-free on the old provider.
   boost::asio::post(
      threadPool_,
      [this, band]()
      {
         try
         {
            if (band != band_)
            {
               logger_->debug("Selecting satellite band: {}",
                              common::GetSatelliteBandName(band));

               provider_.Shutdown();
               provider_ = provider::AwsSatelliteDataProvider(band);
               band_     = band;

               {
                  std::unique_lock lock(dataMutex_);
                  vertices_.clear();
                  moments_.clear();
                  sweepTime_ = std::chrono::system_clock::time_point {};
               }

               ClearSceneCache();

               FetchData();
            }
         }
         catch (const std::exception& ex)
         {
            logger_->error(ex.what());
         }
      });
}

void SatelliteManager::Impl::FetchData()
{
   logger_->debug("Fetching satellite data for band {}",
                  common::GetSatelliteBandName(band_));

   auto now = std::chrono::system_clock::now();

   auto [success, newObjects, totalObjects] = provider_.ListObjects(now);
   if (!success || totalObjects == 0)
   {
      logger_->warn(
         "Could not list satellite scenes for today, trying yesterday...");
      auto yesterday = now - std::chrono::hours(24);
      std::tie(success, newObjects, totalObjects) =
         provider_.ListObjects(yesterday);
   }

   if (success && totalObjects > 0)
   {
      std::string latestKey = provider_.FindLatestKey();
      if (!latestKey.empty())
      {
         logger_->info("Downloading latest satellite scene: {}", latestKey);
         std::string data = provider_.DownloadObject(latestKey);
         if (!data.empty())
         {
            logger_->info("Successfully downloaded satellite scene ({} bytes)",
                          data.size());

            bool isInfrared = (band_ >= common::SatelliteBand::Band07 &&
                               band_ <= common::SatelliteBand::Band16);
            auto satelliteData =
               util::SatelliteReader::ReadMem(data, isInfrared);
            if (satelliteData.has_value())
            {
               // Store cached data under mutex
               {
                  std::unique_lock lock(dataMutex_);
                  vertices_ = std::move(satelliteData->vertices);
                  moments_  = std::move(satelliteData->moments);
                  sweepTime_ =
                     provider::AwsSatelliteDataProvider::GetTimePointFromKey(
                        latestKey);

                  CacheScene(sweepTime_, vertices_, moments_);

                  // Update the available scene times from the provider cache
                  using namespace std::chrono;
                  auto fetchToday = floor<days>(now);
                  for (auto d : {fetchToday - days {1},
                                 fetchToday,
                                 fetchToday + days {1}})
                  {
                     auto points = provider_.GetTimePointsByDate(d);
                     availableTimes_.insert(points.begin(), points.end());
                  }
               }

               Q_EMIT self_->DataUpdated(band_);

               if (refreshEnabled_)
               {
                  ScheduleRefresh();
               }

               return;
            }
         }
      }
   }

   logger_->error("Failed to load satellite scene for band {}",
                  common::GetSatelliteBandName(band_));
   Q_EMIT self_->DataLoadFailed(band_);

   if (refreshEnabled_)
   {
      ScheduleRefresh();
   }
}

void SatelliteManager::Impl::EnableRefresh(bool enabled)
{
   refreshEnabled_ = enabled;

   if (enabled)
   {
      ScheduleRefresh();
   }
   else
   {
      CancelRefresh();
   }
}

void SatelliteManager::Impl::RefreshNow()
{
   CancelRefresh();

   boost::asio::post(threadPool_,
                     [this]()
                     {
                        try
                        {
                           FetchData();
                        }
                        catch (const std::exception& ex)
                        {
                           logger_->error(ex.what());
                        }
                     });
}

void SatelliteManager::Impl::LoadDataForTime(
   common::SatelliteBand band, std::chrono::system_clock::time_point time)
{
   // Cancel any pending refresh (archive mode doesn't use periodic refresh)
   CancelRefresh();

   // Store requested time for the background fetch
   requestedTime_ = time;

   // Defer band switch and data fetch to the thread pool so that
   // any in-progress provider operation completes first.
   boost::asio::post(
      threadPool_,
      [this, band, time]()
      {
         try
         {
            if (band != band_)
            {
               logger_->debug("Switching satellite band for time query: {}",
                              common::GetSatelliteBandName(band));

               provider_.Shutdown();
               provider_ = provider::AwsSatelliteDataProvider(band);
               band_     = band;

               {
                  std::unique_lock lock(dataMutex_);
                  vertices_.clear();
                  moments_.clear();
                  sweepTime_ = std::chrono::system_clock::time_point {};
               }

               ClearSceneCache();
               availableTimes_.clear();

               FetchDataForTime();
            }
         }
         catch (const std::exception& ex)
         {
            logger_->error(ex.what());
         }
      });
}

void SatelliteManager::Impl::FetchDataForTime()
{
   auto requestedTime = requestedTime_;
   if (!requestedTime.has_value())
   {
      return;
   }

   logger_->debug("Fetching satellite data for band {} at {}",
                  common::GetSatelliteBandName(band_),
                  std::chrono::duration_cast<std::chrono::seconds>(
                     requestedTime->time_since_epoch())
                     .count());

   using namespace std::chrono;
   auto now   = system_clock::now();
   auto today = floor<days>(requestedTime.value());

   // List objects for yesterday, today, and tomorrow of the requested time.
   // Skip dates already cached to avoid unnecessary S3 network calls.
   std::vector<system_clock::time_point> dates = {
      today - days {1}, today, today + days {1}};
   for (const auto& date : dates)
   {
      if (date > floor<days>(now))
      {
         break;
      }

      try
      {
         if (!provider_.IsDateCached(date))
         {
            provider_.ListObjects(date);
         }
      }
      catch (const std::exception& ex)
      {
         logger_->warn("Error listing objects for date: {}", ex.what());
      }
   }

   // Find the closest key to the requested time
   std::string key = provider_.FindKey(requestedTime.value());
   if (!key.empty())
   {
      auto sceneTime =
         provider::AwsSatelliteDataProvider::GetTimePointFromKey(key);

      // Check the scene cache first — avoids redundant S3 downloads
      // during loops and scrubbing
      {
         std::unique_lock lock(dataMutex_);
         auto             cacheIt = sceneCache_.find(sceneTime);
         if (cacheIt != sceneCache_.end())
         {
            logger_->debug("Using cached satellite scene: {}", key);
            vertices_  = cacheIt->second.vertices;
            moments_   = cacheIt->second.moments;
            sweepTime_ = sceneTime;

            Q_EMIT self_->DataUpdated(band_);
            return;
         }
      }

      logger_->info("Downloading satellite scene: {}", key);
      std::string data = provider_.DownloadObject(key);
      if (!data.empty())
      {
         logger_->info(
            "Successfully downloaded satellite scene ({} bytes) for time query",
            data.size());

         bool isInfrared    = (band_ >= common::SatelliteBand::Band07 &&
                            band_ <= common::SatelliteBand::Band16);
         auto satelliteData = util::SatelliteReader::ReadMem(data, isInfrared);
         if (satelliteData.has_value())
         {
            {
               std::unique_lock lock(dataMutex_);
               vertices_  = std::move(satelliteData->vertices);
               moments_   = std::move(satelliteData->moments);
               sweepTime_ = sceneTime;

               CacheScene(sceneTime, vertices_, moments_);

               // Update the available scene times from the provider cache
               using namespace std::chrono;
               auto fetchToday = floor<days>(now);
               for (auto d :
                    {fetchToday - days {1}, fetchToday, fetchToday + days {1}})
               {
                  auto points = provider_.GetTimePointsByDate(d);
                  availableTimes_.insert(points.begin(), points.end());
               }
            }

            Q_EMIT self_->DataUpdated(band_);
            return;
         }
      }
   }

   // Fallback: try loading the latest key if requested time wasn't found
   if (!key.empty())
   {
      logger_->warn("Found key but download failed: {}", key);
   }
   else
   {
      logger_->warn("No satellite scene found near requested time for band {}",
                    common::GetSatelliteBandName(band_));
   }

   Q_EMIT self_->DataLoadFailed(band_);
}

void SatelliteManager::Impl::ScheduleRefresh()
{
   using namespace std::chrono_literals;

   if (!refreshEnabled_)
   {
      return;
   }

   std::unique_lock lock(timerMutex_);

   auto                      now      = std::chrono::system_clock::now();
   std::chrono::milliseconds interval = kFastRetryInterval_;

   if (sweepTime_ != std::chrono::system_clock::time_point {})
   {
      auto sinceLastUpdate = now - sweepTime_;
      auto updatePeriod    = 5min; // GOES-19 CONUS imagery cadence

      interval = std::chrono::duration_cast<std::chrono::milliseconds>(
         updatePeriod - sinceLastUpdate);

      if (sinceLastUpdate > updatePeriod * 3)
      {
         interval = kSlowRetryInterval_;
      }
      else if (interval < kFastRetryInterval_)
      {
         interval = kFastRetryInterval_;
      }
   }

   refreshTimer_.expires_after(interval);
   refreshTimer_.async_wait(
      [this](const boost::system::error_code& e)
      {
         if (e == boost::system::errc::success)
         {
            boost::asio::post(threadPool_,
                              [this]()
                              {
                                 try
                                 {
                                    FetchData();
                                 }
                                 catch (const std::exception& ex)
                                 {
                                    logger_->error(ex.what());
                                 }
                              });
         }
         else if (e == boost::asio::error::operation_aborted)
         {
            logger_->debug("Satellite refresh timer cancelled");
         }
         else
         {
            logger_->warn("Satellite refresh timer error: {}", e.message());
         }
      });
}

void SatelliteManager::Impl::CancelRefresh()
{
   std::unique_lock lock(timerMutex_);
   refreshTimer_.cancel();
}

std::chrono::system_clock::time_point
SatelliteManager::Impl::GetAdjacentSceneTime(
   std::chrono::system_clock::time_point time, bool previous) const
{
   std::unique_lock lock(dataMutex_);

   if (previous)
   {
      // Find the first scene time >= time, then go to the one before it
      auto it = availableTimes_.lower_bound(time);
      if (it != availableTimes_.begin())
      {
         return *std::prev(it);
      }
   }
   else
   {
      // Find the first scene time > time
      auto it = availableTimes_.upper_bound(time);
      if (it != availableTimes_.end())
      {
         return *it;
      }
   }

   // No adjacent scene found; return the input time unchanged
   return time;
}

void SatelliteManager::Impl::ClearSceneCache()
{
   sceneCache_.clear();
   sceneCacheOrder_.clear();
}

void SatelliteManager::Impl::PruneSceneCache()
{
   while (sceneCache_.size() > kMaxCachedScenes_)
   {
      auto oldest = sceneCacheOrder_.front();
      sceneCache_.erase(oldest);
      sceneCacheOrder_.pop_front();
   }
}

void SatelliteManager::Impl::CacheScene(
   std::chrono::system_clock::time_point sceneTime,
   const std::vector<float>&             vertices,
   const std::vector<std::uint8_t>&      moments)
{
   // If this scene is already cached, update its position in the LRU order
   auto existing =
      std::find(sceneCacheOrder_.begin(), sceneCacheOrder_.end(), sceneTime);
   if (existing != sceneCacheOrder_.end())
   {
      sceneCacheOrder_.erase(existing);
   }

   sceneCache_[sceneTime] = CachedScene {vertices, moments};
   sceneCacheOrder_.push_back(sceneTime);

   PruneSceneCache();
}

SatelliteManager::SatelliteManager() : p(std::make_unique<Impl>(this)) {}

SatelliteManager::~SatelliteManager() = default;

std::shared_ptr<SatelliteManager> SatelliteManager::Instance()
{
   static std::weak_ptr<SatelliteManager> satelliteManagerReference_ {};
   static std::mutex                      instanceMutex_ {};

   std::unique_lock lock(instanceMutex_);

   std::shared_ptr<SatelliteManager> satelliteManager =
      satelliteManagerReference_.lock();

   if (satelliteManager == nullptr)
   {
      satelliteManager           = std::make_shared<SatelliteManager>();
      satelliteManagerReference_ = satelliteManager;
   }

   return satelliteManager;
}

common::SatelliteBand SatelliteManager::active_band() const
{
   return p->band_;
}

std::chrono::system_clock::time_point SatelliteManager::sweep_time() const
{
   std::unique_lock lock(p->dataMutex_);
   return p->sweepTime_;
}

std::vector<float> SatelliteManager::vertices() const
{
   std::unique_lock lock(p->dataMutex_);
   return p->vertices_;
}

std::vector<std::uint8_t> SatelliteManager::moments() const
{
   std::unique_lock lock(p->dataMutex_);
   return p->moments_;
}

void SatelliteManager::SelectBand(common::SatelliteBand band)
{
   p->SelectBand(band);
}

void SatelliteManager::EnableRefresh(bool enabled)
{
   p->EnableRefresh(enabled);
}

void SatelliteManager::RefreshNow()
{
   p->RefreshNow();
}

void SatelliteManager::LoadDataForTime(
   common::SatelliteBand band, std::chrono::system_clock::time_point time)
{
   p->LoadDataForTime(band, time);
}

std::chrono::system_clock::time_point SatelliteManager::GetAdjacentSceneTime(
   std::chrono::system_clock::time_point time, bool previous) const
{
   return p->GetAdjacentSceneTime(time, previous);
}

} // namespace manager
} // namespace qt
} // namespace scwx
