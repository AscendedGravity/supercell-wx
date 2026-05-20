#include <scwx/util/satellite_reader.hpp>
#include <scwx/provider/aws_satellite_data_provider.hpp>
#include <scwx/common/products.hpp>

#include <gtest/gtest.h>
#include <chrono>

namespace scwx
{
namespace util
{

TEST(SatelliteReaderTest, ReadMemRealData)
{
   using namespace std::chrono_literals;

   // Band 7 is infrared
   provider::AwsSatelliteDataProvider provider(common::SatelliteBand::Band07);

   auto now = std::chrono::system_clock::now();
   // List objects for today
   auto [success, newObjects, totalObjects] = provider.ListObjects(now);
   if (!success || totalObjects == 0)
   {
      // If none, try yesterday
      auto yesterday = now - 24h;
      std::tie(success, newObjects, totalObjects) =
         provider.ListObjects(yesterday);
   }

   ASSERT_TRUE(success);
   ASSERT_GT(totalObjects, 0);

   std::string latestKey = provider.FindLatestKey();
   ASSERT_FALSE(latestKey.empty());

   std::string data = provider.DownloadObject(latestKey);
   ASSERT_FALSE(data.empty());

   auto satelliteData = SatelliteReader::ReadMem(data, true);
   ASSERT_TRUE(satelliteData.has_value());
   EXPECT_GT(satelliteData->vertices.size(), 0);
   EXPECT_GT(satelliteData->moments.size(), 0);
}

} // namespace util
} // namespace scwx
