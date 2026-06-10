#pragma once

#include <scwx/common/products.hpp>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace Aws::S3
{
class S3Client;
} // namespace Aws::S3

namespace scwx::provider
{

/**
 * @brief AWS GOES-19 Satellite Data Provider
 */
class AwsSatelliteDataProvider
{
public:
   explicit AwsSatelliteDataProvider(common::SatelliteBand band);
   ~AwsSatelliteDataProvider();

   AwsSatelliteDataProvider(const AwsSatelliteDataProvider&) = delete;
   AwsSatelliteDataProvider&
   operator=(const AwsSatelliteDataProvider&) = delete;

   AwsSatelliteDataProvider(AwsSatelliteDataProvider&&) noexcept;
   AwsSatelliteDataProvider& operator=(AwsSatelliteDataProvider&&) noexcept;

   /**
    * @brief Lists satellite objects for the date supplied, and adds them to the
    * cache.
    *
    * @param date Date for which to list objects
    *
    * @return - Whether query was successful
    *         - New objects found for the given date
    *         - Total objects found for the given date
    */
   std::tuple<bool, size_t, size_t>
   ListObjects(std::chrono::system_clock::time_point date);

   /**
    * @brief Gets cached satellite time points for the date supplied.
    */
   std::vector<std::chrono::system_clock::time_point>
   GetTimePointsByDate(std::chrono::system_clock::time_point date);

   /**
    * @brief Finds the most recent key in the cache.
    */
   std::string FindLatestKey();

   /**
    * @brief Finds the most recent time in the cache.
    */
   std::chrono::system_clock::time_point FindLatestTime();

   /**
    * @brief Finds the closest key in the cache for the given time (bounded
    * upper).
    */
   std::string FindKey(std::chrono::system_clock::time_point time);

   /**
    * @brief Checks if the given date has already been listed and cached.
    *
    * @param date Date to check
    * @return true if objects for this date are in the local cache
    */
   bool IsDateCached(std::chrono::system_clock::time_point date);

   /**
    * @brief Downloads a satellite object by its S3 key.
    *
    * @param key S3 key
    * @return Raw file bytes downloaded, or empty string on failure.
    */
   std::string DownloadObject(const std::string& key);

   /**
    * @brief Shuts down the provider and stops any in-progress network requests.
    */
   void Shutdown() noexcept;

   /**
    * @brief Parses a GOES filename S3 key to extract its observation start
    * time.
    */
   static std::chrono::system_clock::time_point
   GetTimePointFromKey(const std::string& key);

private:
   class Impl;
   std::unique_ptr<Impl> p;
};

} // namespace scwx::provider
