#include <scwx/qt/manager/wis_manager.hpp>
#include <scwx/network/cpr.hpp>
#include <scwx/util/logger.hpp>
#include <scwx/util/json.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>

#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/json/value.hpp>
#include <cpr/cpr.h>

namespace scwx::qt::manager
{

static const std::string logPrefix_ = "scwx::qt::manager::wis_manager";
static const auto        logger_    = scwx::util::Logger::Create(logPrefix_);

static constexpr auto kAutoRefreshIntervalMs_ = std::chrono::seconds(60);

static const std::string kWisUrl_ = "https://ryanhallyall.com/rhy/wis.json";

class WisManager::Impl
{
public:
   explicit Impl(WisManager* self) : self_(self)
   {
      refreshTimer_.setInterval(kAutoRefreshIntervalMs_);
      QObject::connect(
         &refreshTimer_, &QTimer::timeout, self_, &WisManager::OnRefreshTimer);
   }

   ~Impl()
   {
      fetchCancelled_.store(true);
      threadPool_.join();
   }

   void FetchWisSync(uint64_t fetchId)
   {
      auto response = ::cpr::Get(::cpr::Url {kWisUrl_},
                                 scwx::network::cpr::GetDefaultConnectTimeout(),
                                 scwx::network::cpr::GetDefaultTimeout(),
                                 scwx::network::cpr::GetDefaultLowSpeed(),
                                 scwx::network::cpr::GetHeader());

      if (fetchId != currentFetchId_.load())
      {
         logger_->debug("Discarding stale WIS fetch result (ID: {})", fetchId);
         return;
      }

      if (response.status_code != 200)
      {
         std::string errorMsg = "Failed to fetch WIS data: HTTP " +
                                std::to_string(response.status_code);
         logger_->warn(errorMsg);

         QMetaObject::invokeMethod(
            self_,
            [this, errorMsg]()
            {
               if (!fetchCancelled_.load())
               {
                  Q_EMIT self_->FetchError(QString::fromStdString(errorMsg));
               }
            },
            Qt::QueuedConnection);
         return;
      }

      try
      {
         boost::json::value json =
            scwx::util::json::ReadJsonString(response.text);

         auto& wisObj = json.as_object().at("wis").as_object();

         double score      = wisObj.at("weather_intensity_score").as_double();
         auto&  streamInfo = wisObj.at("todays_stream_info").as_object();
         double threshold =
            streamInfo.at("weather_intensity_score_threshold").as_double();
         std::string mode = streamInfo.at("mode").as_string().c_str();

         {
            const std::lock_guard<std::mutex> lock(dataMutex_);
            weatherIntensityScore_          = score;
            weatherIntensityScoreThreshold_ = threshold;
            mode_                           = mode;
            wisData_                        = std::move(wisObj);
         }

         logger_->info("WIS data updated: score={}, threshold={}, mode={}",
                       score,
                       threshold,
                       mode);

         QMetaObject::invokeMethod(
            self_,
            [this]()
            {
               if (!fetchCancelled_.load())
               {
                  Q_EMIT self_->WisDataUpdated();
               }
            },
            Qt::QueuedConnection);
      }
      catch (const std::exception& ex)
      {
         std::string errorMsg =
            "Failed to parse WIS data: " + std::string(ex.what());
         logger_->warn(errorMsg);

         QMetaObject::invokeMethod(
            self_,
            [this, errorMsg]()
            {
               if (!fetchCancelled_.load())
               {
                  Q_EMIT self_->FetchError(QString::fromStdString(errorMsg));
               }
            },
            Qt::QueuedConnection);
      }
   }

   WisManager* self_;

   QTimer                refreshTimer_;
   std::atomic<bool>     autoRefresh_ {false};
   std::atomic<bool>     fetchCancelled_ {false};
   std::atomic<uint64_t> currentFetchId_ {0};

   mutable std::mutex dataMutex_;
   double             weatherIntensityScore_ {0.0};
   double             weatherIntensityScoreThreshold_ {0.0};
   std::string        mode_;
   boost::json::value wisData_;

   boost::asio::thread_pool threadPool_ {1u};
};

WisManager::WisManager() : QObject(nullptr), p(std::make_unique<Impl>(this)) {}
WisManager::~WisManager() = default;

void WisManager::SetAutoRefresh(bool enabled)
{
   p->autoRefresh_ = enabled;
   if (enabled)
   {
      p->refreshTimer_.start();
   }
   else
   {
      p->refreshTimer_.stop();
   }
}

void WisManager::RefreshNow()
{
   FetchWisAsync();
}

double WisManager::GetWeatherIntensityScore() const
{
   const std::lock_guard<std::mutex> lock(p->dataMutex_);
   return p->weatherIntensityScore_;
}

double WisManager::GetWeatherIntensityScoreThreshold() const
{
   const std::lock_guard<std::mutex> lock(p->dataMutex_);
   return p->weatherIntensityScoreThreshold_;
}

std::string WisManager::GetMode() const
{
   const std::lock_guard<std::mutex> lock(p->dataMutex_);
   return p->mode_;
}

void WisManager::FetchWisAsync()
{
   p->fetchCancelled_.store(false);
   uint64_t fetchId = ++p->currentFetchId_;
   logger_->info("Fetching WIS data (fetch ID: {})", fetchId);

   boost::asio::post(p->threadPool_,
                     [this, fetchId]() { p->FetchWisSync(fetchId); });
}

void WisManager::OnRefreshTimer()
{
   RefreshNow();
}

std::shared_ptr<WisManager> WisManager::Instance()
{
   static std::weak_ptr<WisManager> wisManagerReference_ {};
   static std::mutex                instanceMutex_ {};

   std::unique_lock lock(instanceMutex_);

   std::shared_ptr<WisManager> wisManager = wisManagerReference_.lock();

   if (wisManager == nullptr)
   {
      wisManager           = std::make_shared<WisManager>();
      wisManagerReference_ = wisManager;
   }

   return wisManager;
}

} // namespace scwx::qt::manager
