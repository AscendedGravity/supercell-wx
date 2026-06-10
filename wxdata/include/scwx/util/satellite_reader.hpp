#pragma once

#include <vector>
#include <string>
#include <optional>
#include <cstdint>

namespace scwx
{
namespace util
{

struct SatelliteData
{
   std::vector<float>        vertices;
   std::vector<std::uint8_t> moments;
};

class SatelliteReader
{
public:
   static std::optional<SatelliteData> ReadMem(const std::string& data,
                                               bool               isInfrared);
};

} // namespace util
} // namespace scwx
