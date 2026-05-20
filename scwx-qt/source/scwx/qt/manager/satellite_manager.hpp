#pragma once

#include <scwx/common/products.hpp>
#include <scwx/provider/aws_satellite_data_provider.hpp>

#include <chrono>
#include <memory>
#include <vector>

#include <QObject>

namespace scwx
{
namespace qt
{
namespace manager
{

class SatelliteManager : public QObject
{
   Q_OBJECT
   Q_DISABLE_COPY_MOVE(SatelliteManager)

public:
   explicit SatelliteManager();
   ~SatelliteManager();

   /**
    * @brief Returns the singleton instance.
    */
   static std::shared_ptr<SatelliteManager> Instance();

   /**
    * @brief Gets the current active band.
    */
   common::SatelliteBand active_band() const;

   /**
    * @brief Gets the last sweep time for the active band.
    */
   std::chrono::system_clock::time_point sweep_time() const;

   /**
    * @brief Gets the vertices (lat/lon pairs) for the last loaded scene.
    */
   std::vector<float> vertices() const;

   /**
    * @brief Gets the moment data (pixel values) for the last loaded scene.
    */
   std::vector<std::uint8_t> moments() const;

   /**
    * @brief Selects a satellite band for data fetching. Triggers an immediate
    * fetch on the background thread.
    *
    * @param band Satellite band
    */
   void SelectBand(common::SatelliteBand band);

   /**
    * @brief Enables or disables periodic refresh for the active satellite band.
    *
    * @param enabled Whether to enable refresh
    */
   void EnableRefresh(bool enabled);

   /**
    * @brief Forces an immediate refresh of the current band's data.
    */
   void RefreshNow();

signals:
   /**
    * @brief Emitted when new satellite data has been loaded for the active
    * band.
    * @param band Satellite band
    */
   void DataUpdated(common::SatelliteBand band);

   /**
    * @brief Emitted when a data load fails.
    * @param band Satellite band
    */
   void DataLoadFailed(common::SatelliteBand band);

private:
   class Impl;
   std::unique_ptr<Impl> p;
};

} // namespace manager
} // namespace qt
} // namespace scwx
