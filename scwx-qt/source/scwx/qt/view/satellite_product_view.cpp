#include <scwx/qt/view/satellite_product_view.hpp>
#include <scwx/qt/manager/satellite_manager.hpp>
#include <scwx/util/logger.hpp>

#include <boost/asio.hpp>

namespace scwx::qt::view
{

static const std::string logPrefix_ = "scwx::qt::view::satellite_product_view";
static const auto        logger_    = scwx::util::Logger::Create(logPrefix_);

class SatelliteProductView::Impl
{
public:
   explicit Impl(SatelliteProductView* self, common::SatelliteBand band) :
       self_ {self}, band_ {band}
   {
   }
   ~Impl() { threadPool_.join(); }

   SatelliteProductView*              self_;
   common::SatelliteBand              band_;

   boost::asio::thread_pool threadPool_ {1u};

   std::vector<float>                     vertices_ {};
   std::vector<std::uint8_t>              moments_ {};
   std::shared_ptr<common::ColorTable>    colorTable_ {nullptr};
   std::vector<boost::gil::rgba8_pixel_t> colorTableLut_ {};

   std::chrono::system_clock::time_point sweepTime_ {};

   std::shared_ptr<manager::SatelliteManager> satelliteManager_ {};
   QMetaObject::Connection                    dataUpdatedConnection_ {};
};

SatelliteProductView::SatelliteProductView(
   common::SatelliteBand                         band,
   std::shared_ptr<manager::RadarProductManager> radarProductManager) :
    RadarProductView(radarProductManager), p(std::make_unique<Impl>(this, band))
{
   ConnectRadarProductManager();
}

SatelliteProductView::~SatelliteProductView() = default;

std::shared_ptr<common::ColorTable> SatelliteProductView::color_table() const
{
   return p->colorTable_;
}

const std::vector<boost::gil::rgba8_pixel_t>&
SatelliteProductView::color_table_lut() const
{
   if (p->colorTableLut_.empty())
   {
      return RadarProductView::color_table_lut();
   }
   return p->colorTableLut_;
}

std::uint16_t SatelliteProductView::color_table_min() const
{
   return 0;
}

std::uint16_t SatelliteProductView::color_table_max() const
{
   return 255;
}

float SatelliteProductView::unit_scale() const
{
   return 1.0f;
}

std::string SatelliteProductView::units() const
{
   bool isInfrared = (p->band_ >= common::SatelliteBand::Band07 &&
                      p->band_ <= common::SatelliteBand::Band16);
   return isInfrared ? "K" : "";
}

std::uint16_t SatelliteProductView::vcp() const
{
   return 0;
}

const std::vector<float>& SatelliteProductView::vertices() const
{
   return p->vertices_;
}

std::chrono::system_clock::time_point SatelliteProductView::sweep_time() const
{
   return p->sweepTime_;
}

void SatelliteProductView::LoadColorTable(
   std::shared_ptr<common::ColorTable> colorTable)
{
   p->colorTable_ = colorTable;

   // Defer LUT generation during band transitions. If there's no valid
   // vertex data yet, the LUT will be regenerated when new data arrives
   // via the DataUpdated handler or ComputeSweep (both call
   // UpdateColorTableLut after populating vertices).
   if (!p->vertices_.empty())
   {
      UpdateColorTableLut();
   }
}

void SatelliteProductView::SelectProduct(const std::string& productName)
{
   common::SatelliteBand newBand = common::GetSatelliteBand(productName);
   if (newBand != common::SatelliteBand::Unknown)
   {
      p->band_ = newBand;

      // Clear stale data immediately so the layer doesn't render old
      // vertices with the new band's color table during the fetch window.
      p->vertices_.clear();
      p->moments_.clear();
      p->sweepTime_ = {};

      set_load_status(types::RadarProductLoadStatus::ProductNotAvailable);
      Q_EMIT SweepNotComputed(types::NoUpdateReason::NotAvailable);

      Update();
   }
}

common::RadarProductGroup SatelliteProductView::GetRadarProductGroup() const
{
   return common::RadarProductGroup::Satellite;
}

std::string SatelliteProductView::GetRadarProductName() const
{
   return common::GetSatelliteBandName(p->band_);
}

std::tuple<const void*, std::size_t, std::size_t>
SatelliteProductView::GetMomentData() const
{
   const void* data          = p->moments_.data();
   std::size_t dataSize      = p->moments_.size() * sizeof(std::uint8_t);
   std::size_t componentSize = 1;

   return std::tie(data, dataSize, componentSize);
}

std::optional<std::uint16_t> SatelliteProductView::GetBinLevel(
   const common::Coordinate& /* coordinate */) const
{
   return {};
}

std::optional<wsr88d::DataLevelCode>
SatelliteProductView::GetDataLevelCode(std::uint16_t /* level */) const
{
   return {};
}

std::optional<float>
SatelliteProductView::GetDataValue(std::uint16_t /* level */) const
{
   return {};
}

std::shared_ptr<SatelliteProductView> SatelliteProductView::Create(
   common::SatelliteBand                         band,
   std::shared_ptr<manager::RadarProductManager> radarProductManager)
{
   return std::make_shared<SatelliteProductView>(band, radarProductManager);
}

boost::asio::thread_pool& SatelliteProductView::thread_pool()
{
   return p->threadPool_;
}

void SatelliteProductView::ConnectRadarProductManager()
{
   p->satelliteManager_ = manager::SatelliteManager::Instance();

   p->dataUpdatedConnection_ = QObject::connect(
      p->satelliteManager_.get(),
      &manager::SatelliteManager::DataUpdated,
      this,
      [this](common::SatelliteBand band)
      {
         if (band == p->band_)
         {
            // Copy cached data from manager
            p->vertices_  = p->satelliteManager_->vertices();
            p->moments_   = p->satelliteManager_->moments();
            p->sweepTime_ = p->satelliteManager_->sweep_time();

            UpdateColorTableLut();

            set_load_status(types::RadarProductLoadStatus::ProductLoaded);
            Q_EMIT SweepComputed();
         }
      });
}

void SatelliteProductView::DisconnectRadarProductManager()
{
   if (p->satelliteManager_ != nullptr)
   {
      QObject::disconnect(p->dataUpdatedConnection_);
      p->satelliteManager_.reset();
   }
}

void SatelliteProductView::UpdateColorTableLut()
{
   logger_->debug("UpdateColorTableLut()");

   if (p->colorTable_ != nullptr && p->colorTable_->IsValid())
   {
      bool isInfrared = (p->band_ >= common::SatelliteBand::Band07 &&
                         p->band_ <= common::SatelliteBand::Band16);

      p->colorTableLut_.resize(256);
      for (size_t i = 0; i < 256; ++i)
      {
         float physicalVal = 0.0f;
         if (isInfrared)
         {
            physicalVal = 330.0f - (static_cast<float>(i) * 150.0f / 255.0f);
         }
         else
         {
            physicalVal = static_cast<float>(i) / 255.0f;
         }
         p->colorTableLut_[i] = p->colorTable_->Color(physicalVal);
      }
   }
   else
   {
      p->colorTableLut_.resize(256);
      if (p->band_ >= common::SatelliteBand::Band08 &&
          p->band_ <= common::SatelliteBand::Band10)
      {
         // Water Vapor Fallback: Tropospheric moisture enhancement (180K to
         // 330K) Dry upper-level air is warm (oranges/yellows), moist layers
         // are cold (blues/whites)
         for (size_t i = 0; i < 256; ++i)
         {
            std::uint8_t r = 0, g = 0, b = 0;
            if (i <= 85)
            {
               // 330K to 280K: dark red to orange/yellow transition
               float t = static_cast<float>(i) / 85.0f;
               r       = static_cast<std::uint8_t>(32.0f + 223.0f * t);
               g       = static_cast<std::uint8_t>(220.0f * t);
               b       = 0;
            }
            else if (i <= 170)
            {
               // 280K to 230K: orange/yellow to deep blue transition
               float t = (static_cast<float>(i) - 85.0f) / 85.0f;
               r       = static_cast<std::uint8_t>(255.0f * (1.0f - t));
               g       = static_cast<std::uint8_t>(220.0f * (1.0f - t));
               b       = static_cast<std::uint8_t>(200.0f * t);
            }
            else
            {
               // 230K to 180K: deep blue to pure white transition
               float t = (static_cast<float>(i) - 170.0f) / 85.0f;
               r       = static_cast<std::uint8_t>(255.0f * t);
               g       = static_cast<std::uint8_t>(255.0f * t);
               b       = static_cast<std::uint8_t>(200.0f + 55.0f * t);
            }
            p->colorTableLut_[i] = boost::gil::rgba8_pixel_t(r, g, b, 255);
         }
      }
      else if (p->band_ >= common::SatelliteBand::Band07 &&
               p->band_ <= common::SatelliteBand::Band16)
      {
         // Infrared Fallback: Convective storm-top enhancement (180K to 330K)
         // Grayscale for warm ground/low clouds, color gradients for cold storm
         // tops (< 240K)
         for (size_t i = 0; i < 256; ++i)
         {
            std::uint8_t r = 0, g = 0, b = 0;
            if (i <= 153)
            {
               // 330K to 240K: grayscale (black to white)
               std::uint8_t gray = static_cast<std::uint8_t>(
                  static_cast<float>(i) / 153.0f * 255.0f);
               r = g = b = gray;
            }
            else if (i <= 187)
            {
               // 240K to 220K: white to cyan to blue transition
               float t = (static_cast<float>(i) - 153.0f) / (187.0f - 153.0f);
               r       = static_cast<std::uint8_t>(255.0f * (1.0f - t));
               g       = static_cast<std::uint8_t>(255.0f * (1.0f - 0.5f * t));
               b       = 255;
            }
            else if (i <= 221)
            {
               // 220K to 200K: blue to green to yellow transition
               float t = (static_cast<float>(i) - 187.0f) / (221.0f - 187.0f);
               r       = static_cast<std::uint8_t>(255.0f * t);
               g       = static_cast<std::uint8_t>(128.0f + 127.0f * t);
               b       = static_cast<std::uint8_t>(255.0f * (1.0f - t));
            }
            else if (i <= 238)
            {
               // 200K to 190K: yellow to bright red transition
               float t = (static_cast<float>(i) - 221.0f) / (238.0f - 221.0f);
               r       = 255;
               g       = static_cast<std::uint8_t>(255.0f * (1.0f - t));
               b       = 0;
            }
            else
            {
               // 190K to 180K: red to magenta to white transition
               float t = (static_cast<float>(i) - 238.0f) / (255.0f - 238.0f);
               r       = 255;
               g       = static_cast<std::uint8_t>(255.0f * t);
               b       = static_cast<std::uint8_t>(255.0f * t);
            }
            p->colorTableLut_[i] = boost::gil::rgba8_pixel_t(r, g, b, 255);
         }
      }
      else
      {
         // Visible & Near-IR Fallback: Simple premium grayscale reflectance
         // (0.0 to 1.0)
         for (size_t i = 0; i < 256; ++i)
         {
            std::uint8_t val = static_cast<std::uint8_t>(i);
            p->colorTableLut_[i] =
               boost::gil::rgba8_pixel_t(val, val, val, 255);
         }
      }
   }

   Q_EMIT ColorTableLutUpdated();
}

void SatelliteProductView::ComputeSweep()
{
   logger_->trace("ComputeSweep()");

   if (p->satelliteManager_ == nullptr)
   {
      set_load_status(types::RadarProductLoadStatus::ProductNotAvailable);
      Q_EMIT SweepNotComputed(types::NoUpdateReason::NotAvailable);
      return;
   }

   auto selectedTime = selected_time();

   // If no time has been explicitly selected (initial state before timeline
   // scrub), fall back to the existing live data behavior
   if (selectedTime == std::chrono::system_clock::time_point {})
   {
      if (p->satelliteManager_->active_band() == p->band_)
      {
         p->vertices_  = p->satelliteManager_->vertices();
         p->moments_   = p->satelliteManager_->moments();
         p->sweepTime_ = p->satelliteManager_->sweep_time();

         if (!p->vertices_.empty())
         {
            UpdateColorTableLut();
            set_load_status(types::RadarProductLoadStatus::ProductLoaded);
            Q_EMIT SweepComputed();
            return;
         }
      }

      set_load_status(types::RadarProductLoadStatus::LoadingProduct);
      return;
   }

   // A time has been selected (timeline scrubbing or live mode).
   // Check if the already-cached data is close enough to the selected time.
   auto cachedSweepTime = p->satelliteManager_->sweep_time();
   if (cachedSweepTime != std::chrono::system_clock::time_point {} &&
       p->satelliteManager_->active_band() == p->band_)
   {
      auto diff = std::chrono::duration_cast<std::chrono::seconds>(
         cachedSweepTime - selectedTime);

      // Tight threshold (1s): the manager's scene cache handles
      // deduplication for previously fetched scenes. We only skip
      // the manager when the exact same time is already loaded.
      if (std::abs(diff.count()) < 1)
      {
         p->vertices_  = p->satelliteManager_->vertices();
         p->moments_   = p->satelliteManager_->moments();
         p->sweepTime_ = cachedSweepTime;

         if (!p->vertices_.empty())
         {
            UpdateColorTableLut();
            set_load_status(types::RadarProductLoadStatus::ProductLoaded);
            Q_EMIT SweepComputed();
            return;
         }
      }
   }

   // Need to load data for the selected time (async).
   // The result will arrive via DataUpdated signal in
   // ConnectRadarProductManager.
   p->satelliteManager_->LoadDataForTime(p->band_, selectedTime);
   set_load_status(types::RadarProductLoadStatus::LoadingProduct);
}

} // namespace scwx::qt::view
