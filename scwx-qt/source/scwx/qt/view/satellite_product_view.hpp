#pragma once

#include <scwx/common/products.hpp>
#include <scwx/qt/view/radar_product_view.hpp>

#include <memory>

namespace scwx::qt::manager
{
class SatelliteManager;
}

namespace scwx::qt::view
{

class SatelliteProductView : public RadarProductView
{
   Q_OBJECT

public:
   explicit SatelliteProductView(
      common::SatelliteBand                         band,
      std::shared_ptr<manager::RadarProductManager> radarProductManager);
   ~SatelliteProductView() override;

   SatelliteProductView(const SatelliteProductView&)            = delete;
   SatelliteProductView(SatelliteProductView&&)                 = delete;
   SatelliteProductView& operator=(const SatelliteProductView&) = delete;
   SatelliteProductView& operator=(SatelliteProductView&&)      = delete;

   [[nodiscard]] std::shared_ptr<common::ColorTable>
   color_table() const override;
   [[nodiscard]] const std::vector<boost::gil::rgba8_pixel_t>&
                                           color_table_lut() const override;
   [[nodiscard]] std::uint16_t             color_table_min() const override;
   [[nodiscard]] std::uint16_t             color_table_max() const override;
   [[nodiscard]] float                     unit_scale() const override;
   [[nodiscard]] std::string               units() const override;
   [[nodiscard]] std::uint16_t             vcp() const override;
   [[nodiscard]] const std::vector<float>& vertices() const override;
   [[nodiscard]] std::chrono::system_clock::time_point
   sweep_time() const override;

   void LoadColorTable(std::shared_ptr<common::ColorTable> colorTable) override;
   void SelectProduct(const std::string& productName) override;

   [[nodiscard]] common::RadarProductGroup
                             GetRadarProductGroup() const override;
   [[nodiscard]] std::string GetRadarProductName() const override;
   [[nodiscard]] std::tuple<const void*, std::size_t, std::size_t>
   GetMomentData() const override;

   [[nodiscard]] std::optional<std::uint16_t>
   GetBinLevel(const common::Coordinate& coordinate) const override;
   [[nodiscard]] std::optional<wsr88d::DataLevelCode>
   GetDataLevelCode(std::uint16_t level) const override;
   [[nodiscard]] std::optional<float>
   GetDataValue(std::uint16_t level) const override;

   static std::shared_ptr<SatelliteProductView>
   Create(common::SatelliteBand                         band,
          std::shared_ptr<manager::RadarProductManager> radarProductManager);

protected:
   boost::asio::thread_pool& thread_pool() override;

   void ConnectRadarProductManager() override;
   void DisconnectRadarProductManager() override;
   void UpdateColorTableLut() override;

protected slots:
   void ComputeSweep() override;

private:
   class Impl;
   std::unique_ptr<Impl> p;
};

} // namespace scwx::qt::view
