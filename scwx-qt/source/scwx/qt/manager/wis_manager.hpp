#pragma once

#include <memory>
#include <string>

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
