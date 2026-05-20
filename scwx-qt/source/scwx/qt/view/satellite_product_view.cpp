#include <scwx/qt/view/satellite_product_view.hpp>
#include <scwx/provider/aws_satellite_data_provider.hpp>
#include <scwx/util/satellite_reader.hpp>
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
       self_ {self}, band_ {band}, provider_ {band}
   {
   }
   ~Impl()
   {
      provider_.Shutdown();
      threadPool_.join();
   }

   SatelliteProductView*              self_;
   common::SatelliteBand              band_;
   provider::AwsSatelliteDataProvider provider_;

   boost::asio::thread_pool threadPool_ {1u};

   std::vector<float>                     vertices_ {};
   std::vector<std::uint8_t>              moments_ {};
   std::shared_ptr<common::ColorTable>    colorTable_ {nullptr};
   std::vector<boost::gil::rgba8_pixel_t> colorTableLut_ {};

   std::chrono::system_clock::time_point sweepTime_ {};
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
   UpdateColorTableLut();
}

void SatelliteProductView::SelectProduct(const std::string& productName)
{
   common::SatelliteBand newBand = common::GetSatelliteBand(productName);
   if (newBand != common::SatelliteBand::Unknown)
   {
      p->band_     = newBand;
      p->provider_ = provider::AwsSatelliteDataProvider(newBand);
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

void SatelliteProductView::ConnectRadarProductManager() {}

void SatelliteProductView::DisconnectRadarProductManager() {}

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

   set_load_status(types::RadarProductLoadStatus::LoadingProduct);

   auto now = std::chrono::system_clock::now();
   logger_->info("Querying available satellite scenes for band {}",
                 common::GetSatelliteBandName(p->band_));

   auto [success, newObjects, totalObjects] = p->provider_.ListObjects(now);
   if (!success || totalObjects == 0)
   {
      logger_->warn(
         "Could not list satellite scenes for today, trying yesterday...");
      auto yesterday = now - std::chrono::hours(24);
      std::tie(success, newObjects, totalObjects) =
         p->provider_.ListObjects(yesterday);
   }

   if (success && totalObjects > 0)
   {
      std::string latestKey = p->provider_.FindLatestKey();
      if (!latestKey.empty())
      {
         logger_->info("Downloading latest GOES-19 satellite scene: {}",
                       latestKey);
         std::string data = p->provider_.DownloadObject(latestKey);
         if (!data.empty())
         {
            logger_->info("Successfully downloaded satellite scene ({} bytes)",
                          data.size());

            bool isInfrared = (p->band_ >= common::SatelliteBand::Band07 &&
                               p->band_ <= common::SatelliteBand::Band16);
            auto satelliteData =
               util::SatelliteReader::ReadMem(data, isInfrared);
            if (satelliteData.has_value())
            {
               p->vertices_ = std::move(satelliteData->vertices);
               p->moments_  = std::move(satelliteData->moments);

               p->sweepTime_ =
                  provider::AwsSatelliteDataProvider::GetTimePointFromKey(
                     latestKey);

               UpdateColorTableLut();

               set_load_status(types::RadarProductLoadStatus::ProductLoaded);
               Q_EMIT SweepComputed();
               return;
            }
         }
      }
   }

   logger_->error("Failed to load satellite scene for band {}",
                  common::GetSatelliteBandName(p->band_));
   set_load_status(types::RadarProductLoadStatus::ProductNotAvailable);
   Q_EMIT SweepNotComputed(types::NoUpdateReason::NotAvailable);
}

} // namespace scwx::qt::view
