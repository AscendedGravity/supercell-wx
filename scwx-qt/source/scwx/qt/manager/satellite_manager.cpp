#include <scwx/qt/manager/satellite_manager.hpp>
#include <scwx/provider/aws_satellite_data_provider.hpp>
#include <scwx/util/satellite_reader.hpp>
#include <scwx/util/logger.hpp>

#include <atomic>
#include <mutex>
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
   void ScheduleRefresh();
   void CancelRefresh();

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
};

void SatelliteManager::Impl::SelectBand(common::SatelliteBand band)
{
   if (band != band_)
   {
      logger_->debug("Selecting satellite band: {}",
                     common::GetSatelliteBandName(band));

      // Shutdown old provider and create new one
      provider_.Shutdown();
      provider_ = provider::AwsSatelliteDataProvider(band);
      band_     = band;
   }

   // Cancel any pending refresh timer
   CancelRefresh();

   // Post FetchData to thread pool
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

} // namespace manager
} // namespace qt
} // namespace scwx
