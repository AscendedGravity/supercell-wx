#include <scwx/qt/manager/wis_manager.hpp>
#include <scwx/network/cpr.hpp>
#include <scwx/util/logger.hpp>
#include <scwx/util/json.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>
#include <utility>

#include <QDateTime>

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

         auto& jsonRoot = json.as_object();
         auto& wisObj   = jsonRoot.at("wis").as_object();

         double score =
            wisObj.at("weather_intensity_score").to_number<double>();
         auto&  streamInfo = wisObj.at("todays_stream_info").as_object();
         double threshold  = streamInfo.at("weather_intensity_score_threshold")
                               .to_number<double>();
         std::string mode = streamInfo.at("mode").as_string().c_str();

         double score30mAgo =
            wisObj.at("weather_intensity_score_30m_ago").to_number<double>();
         double score30mFromNow =
            wisObj.at("weather_intensity_score_30m_from_now")
               .to_number<double>();

         std::string eventStart =
            streamInfo.at("event_start").as_string().c_str();
         std::string eventPeak =
            streamInfo.at("event_peak").as_string().c_str();
         std::string standbyUntil =
            streamInfo.at("standby_until").as_string().c_str();
         std::string eventEnd = streamInfo.at("event_end").as_string().c_str();

         std::string forecastReasoning =
            wisObj.at("forecast_reasoning").as_string().c_str();
         std::string timestamp = wisObj.at("timestamp").as_string().c_str();

         // Parse score history (last ~30 minutes of actual data)
         std::vector<double> scoreHistory;
         if (jsonRoot.contains("score_history"))
         {
            auto& shArr = jsonRoot.at("score_history").as_array();
            for (const auto& entry : shArr)
            {
               auto&  obj = entry.as_object();
               double entryScore =
                  obj.at("weather_intensity_score").to_number<double>();
               std::string ts = obj.at("timestamp").as_string().c_str();

               // Filter to last ~30 minutes
               // Parse ISO timestamp and check if within 30 minutes
               QDateTime entryTime = QDateTime::fromString(
                  QString::fromStdString(ts), Qt::ISODate);
               QDateTime now = QDateTime::currentDateTimeUtc();
               if (entryTime.isValid() && entryTime.secsTo(now) <= 30 * 60)
               {
                  scoreHistory.push_back(entryScore);
               }
            }
         }

         // Parse daily outlook scores
         std::vector<DailyOutlook> dailyOutlooks;
         if (jsonRoot.contains("daily_outlook_scores"))
         {
            auto& dosObj = jsonRoot.at("daily_outlook_scores").as_object();
            for (int day = 1; day <= 7; ++day)
            {
               std::string dayKey = "day" + std::to_string(day);
               if (!dosObj.contains(dayKey))
               {
                  continue;
               }
               auto& dayObj = dosObj.at(dayKey).as_object();
               auto& pubObj = dayObj.at("public").as_object();

               DailyOutlook outlook;
               outlook.dos_score = dayObj.at("dos_score").to_number<double>();
               outlook.day_name  = pubObj.at("day_name").as_string().c_str();
               outlook.date      = pubObj.at("date").as_string().c_str();

               // chance fields are under "public" with full Ryan-specific names
               if (pubObj.contains("chance_ryan_goes_live_this_day"))
               {
                  outlook.chance_live =
                     pubObj.at("chance_ryan_goes_live_this_day")
                        .as_string()
                        .c_str();
               }
               if (pubObj.contains("chance_ryan_makes_a_video_this_day"))
               {
                  outlook.chance_video =
                     pubObj.at("chance_ryan_makes_a_video_this_day")
                        .as_string()
                        .c_str();
               }

               // threshold and mode are in stream_info, not public
               if (dayObj.contains("stream_info"))
               {
                  auto& dayStreamInfo = dayObj.at("stream_info").as_object();
                  if (dayStreamInfo.contains("threshold"))
                  {
                     outlook.threshold =
                        dayStreamInfo.at("threshold").to_number<double>();
                  }
                  if (dayStreamInfo.contains("mode"))
                  {
                     outlook.mode =
                        dayStreamInfo.at("mode").as_string().c_str();
                  }
               }

               dailyOutlooks.push_back(std::move(outlook));
            }
         }

         {
            const std::lock_guard<std::mutex> lock(dataMutex_);
            weatherIntensityScore_           = score;
            weatherIntensityScoreThreshold_  = threshold;
            mode_                            = mode;
            weatherIntensityScore30mAgo_     = score30mAgo;
            weatherIntensityScore30mFromNow_ = score30mFromNow;
            eventStart_                      = eventStart;
            eventPeak_                       = eventPeak;
            standbyUntil_                    = standbyUntil;
            eventEnd_                        = eventEnd;
            forecastReasoning_               = forecastReasoning;
            timestamp_                       = timestamp;
            scoreHistory_                    = std::move(scoreHistory);
            dailyOutlooks_                   = std::move(dailyOutlooks);
            wisData_                         = std::move(wisObj);
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

   mutable std::mutex                    dataMutex_;
   double                                weatherIntensityScore_ {0.0};
   double                                weatherIntensityScoreThreshold_ {0.0};
   std::string                           mode_;
   double                                weatherIntensityScore30mAgo_ {0.0};
   double                                weatherIntensityScore30mFromNow_ {0.0};
   std::string                           eventStart_;
   std::string                           eventPeak_;
   std::string                           standbyUntil_;
   std::string                           eventEnd_;
   std::string                           forecastReasoning_;
   std::string                           timestamp_;
   std::vector<double>                   scoreHistory_ {};
   std::vector<WisManager::DailyOutlook> dailyOutlooks_ {};
   boost::json::value                    wisData_;

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

double WisManager::GetWeatherIntensityScore30mAgo() const
{
   const std::lock_guard<std::mutex> lock(p->dataMutex_);
   return p->weatherIntensityScore30mAgo_;
}

double WisManager::GetWeatherIntensityScore30mFromNow() const
{
   const std::lock_guard<std::mutex> lock(p->dataMutex_);
   return p->weatherIntensityScore30mFromNow_;
}

std::string WisManager::GetEventStart() const
{
   const std::lock_guard<std::mutex> lock(p->dataMutex_);
   return p->eventStart_;
}

std::string WisManager::GetEventPeak() const
{
   const std::lock_guard<std::mutex> lock(p->dataMutex_);
   return p->eventPeak_;
}

std::string WisManager::GetStandbyUntil() const
{
   const std::lock_guard<std::mutex> lock(p->dataMutex_);
   return p->standbyUntil_;
}

std::string WisManager::GetEventEnd() const
{
   const std::lock_guard<std::mutex> lock(p->dataMutex_);
   return p->eventEnd_;
}

std::string WisManager::GetForecastReasoning() const
{
   const std::lock_guard<std::mutex> lock(p->dataMutex_);
   return p->forecastReasoning_;
}

std::string WisManager::GetTimestamp() const
{
   const std::lock_guard<std::mutex> lock(p->dataMutex_);
   return p->timestamp_;
}

std::vector<double> WisManager::GetScoreHistory() const
{
   const std::lock_guard<std::mutex> lock(p->dataMutex_);
   return p->scoreHistory_;
}

std::vector<WisManager::DailyOutlook> WisManager::GetDailyOutlookScores() const
{
   const std::lock_guard<std::mutex> lock(p->dataMutex_);
   return p->dailyOutlooks_;
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
