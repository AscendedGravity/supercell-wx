#pragma once

#include <scwx/util/iterator.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace scwx
{
namespace common
{

enum class RadarProductGroup
{
   Level2,
   Level3,
   Satellite,
   Unknown
};
typedef util::Iterator<RadarProductGroup,
                       RadarProductGroup::Level2,
                       RadarProductGroup::Satellite>
   RadarProductGroupIterator;

enum class Level2Product
{
   Reflectivity,
   Velocity,
   StormRelativeVelocity,
   SpectrumWidth,
   DifferentialReflectivity,
   DifferentialPhase,
   CorrelationCoefficient,
   ClutterFilterPowerRemoved,
   HydrometeorClassification,
   TornadoDebrisSignature,
   Unknown
};
typedef util::Iterator<Level2Product,
                       Level2Product::Reflectivity,
                       Level2Product::TornadoDebrisSignature>
   Level2ProductIterator;

enum class Level3ProductCategory
{
   Reflectivity,
   Velocity,
   StormRelativeVelocity,
   SpectrumWidth,
   DifferentialReflectivity,
   SpecificDifferentialPhase,
   CorrelationCoefficient,
   VerticallyIntegratedLiquid,
   EchoTops,
   HydrometeorClassification,
   PrecipitationAccumulation,
   Unknown
};
typedef util::Iterator<Level3ProductCategory,
                       Level3ProductCategory::Reflectivity,
                       Level3ProductCategory::PrecipitationAccumulation>
   Level3ProductCategoryIterator;

typedef std::unordered_map<
   common::Level3ProductCategory,
   std::unordered_map<std::string, std::vector<std::string>>>
   Level3ProductCategoryMap;

const std::string& GetRadarProductGroupName(RadarProductGroup group);
RadarProductGroup  GetRadarProductGroup(const std::string& name);

const std::string& GetLevel2Name(Level2Product product);
const std::string& GetLevel2Description(Level2Product product);
const std::string& GetLevel2Palette(Level2Product product);
Level2Product      GetLevel2Product(const std::string& name);

const std::string& GetLevel3CategoryName(Level3ProductCategory category);
const std::string& GetLevel3CategoryDescription(Level3ProductCategory category);
std::string
GetLevel3CategoryDefaultProduct(Level3ProductCategory           category,
                                const Level3ProductCategoryMap& categoryMap);
Level3ProductCategory GetLevel3Category(const std::string& categoryName);
Level3ProductCategory
GetLevel3CategoryByProduct(const std::string& productName);
Level3ProductCategory GetLevel3CategoryByAwipsId(const std::string& awipsId);
const std::string&    GetLevel3Palette(int16_t productCode);

std::string        GetLevel3ProductByAwipsId(const std::string& awipsId);
const std::string& GetLevel3ProductDescription(const std::string& productName);
int16_t            GetLevel3ProductCodeByAwipsId(const std::string& awipsId);
int16_t GetLevel3ProductCodeByProduct(const std::string& productName);
const std::vector<std::string>&
GetLevel3ProductsByCategory(Level3ProductCategory category);
const std::vector<std::string>&
GetLevel3AwipsIdsByProduct(const std::string& productName);

inline constexpr size_t kLevel3ProductMaxTilts = 9;

enum class SatelliteBand
{
   Band01,
   Band02,
   Band03,
   Band04,
   Band05,
   Band06,
   Band07,
   Band08,
   Band09,
   Band10,
   Band11,
   Band12,
   Band13,
   Band14,
   Band15,
   Band16,
   Unknown
};
typedef util::
   Iterator<SatelliteBand, SatelliteBand::Band01, SatelliteBand::Band16>
      SatelliteBandIterator;

const std::string& GetSatelliteBandName(SatelliteBand band);
const std::string& GetSatelliteBandDescription(SatelliteBand band);
SatelliteBand      GetSatelliteBand(const std::string& name);

} // namespace common
} // namespace scwx
