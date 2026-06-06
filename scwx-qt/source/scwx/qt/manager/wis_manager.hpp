#pragma once

#include <memory>
#include <string>
#include <vector>
#include <utility>

#include <QObject>
#include <QTimer>

namespace scwx::qt::manager
{

class WisManager : public QObject
{
   Q_OBJECT
   Q_DISABLE_COPY_MOVE(WisManager)

public:
   explicit WisManager();
   ~WisManager();

   void SetAutoRefresh(bool enabled);
   void RefreshNow();

   double      GetWeatherIntensityScore() const;
   double      GetWeatherIntensityScoreThreshold() const;
   std::string GetMode() const;

   double      GetWeatherIntensityScore30mAgo() const;
   double      GetWeatherIntensityScore30mFromNow() const;
   std::string GetEventStart() const;
   std::string GetEventPeak() const;
   std::string GetStandbyUntil() const;
   std::string GetEventEnd() const;
   std::string GetForecastReasoning() const;
   std::string GetTimestamp() const;
   std::vector<std::pair<int, double>> GetForecastChanges() const;

   struct DailyOutlook
   {
      double      dos_score;
      std::string day_name;
      std::string date;
      std::string chance_live;
      std::string chance_video;
      double      threshold;
      std::string mode;
   };
   std::vector<DailyOutlook> GetDailyOutlookScores() const;

   static std::shared_ptr<WisManager> Instance();

signals:
   void WisDataUpdated();
   void FetchError(const QString& message);

private slots:
   void OnRefreshTimer();

private:
   void FetchWisAsync();

   class Impl;
   std::unique_ptr<Impl> p;
};

} // namespace scwx::qt::manager
