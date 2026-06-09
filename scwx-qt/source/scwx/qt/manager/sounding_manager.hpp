#pragma once

#include <memory>

#include <QObject>
#include <QString>

class QProcess;

namespace scwx::qt::manager
{

class SoundingManagerImpl;

/**
 * @brief Manager for generating soundings using the rustwx sounding_plot
 * binary.
 *
 * This singleton manager invokes the external sounding_plot CLI tool via
 * QProcess to generate SHARPpy-style sounding PNGs. Results are cached in
 * memory for the duration of the session to avoid redundant network fetches.
 *
 * The date is sourced from TimelineManager::GetSelectedTime() unless explicitly
 * overridden via the dateStr parameter.
 */
class SoundingManager : public QObject
{
   Q_OBJECT
   Q_DISABLE_COPY_MOVE(SoundingManager)

public:
   explicit SoundingManager();
   ~SoundingManager();

   static SoundingManager& Instance();

   /**
    * @brief Locate the sounding_plot binary.
    *
    * Search order:
    *   1. User-configured path in settings
    *   2. Next to the application executable
    *   3. PATH environment variable
    *
    * @return Absolute path to the binary, or an empty string if not found.
    */
   static QString FindBinary();

   /**
    * @brief Set a custom path to the sounding_plot binary.
    * @param path Absolute path to the executable
    */
   static void SetBinaryPath(const QString& path);

   /**
    * @brief Remove expired sounding cache entries.
    *
    * Keeps only the \p keepMostRecent most recent cache directories and clears
    * the in-memory index so stale entries are re-fetched.
    *
    * The rustwx library also caches raw GRIB data in
    * <tt>$HOME/.cache/rustwx/</tt>.
    * Call \c ClearRustwxCache() to remove that data as well.
    *
    * @param keepMostRecent Number of directories to retain (default: 5).
    */
   static void PruneCache(std::size_t keepMostRecent = 5);

   /**
    * @brief Remove the rustwx GRIB data cache directory.
    *
    * The rustwx library caches downloaded GRIB files under
    * <tt>$HOME/.cache/rustwx/</tt>.  Deleting this forces rustwx
    * to re-download model data on the next request.
    */
   static void ClearRustwxCache();

public slots:
   /**
    * @brief Request a sounding image from the rustwx sounding_plot tool.
    *
    * @param lat          Latitude in degrees
    * @param lon          Longitude in degrees
    * @param model        Model identifier (hrrr, gfs, rap, nam,
    * ecmwf-open-data, rrfs-a)
    * @param cycle        Model cycle hour (0, 6, 12, 18)
    * @param forecastHour Forecast hour (e.g. 0 for analysis, 1, 3, 6, etc.)
    * @param dateStr      Date in YYYYMMDD format. If empty, uses the currently
    *                     selected time from TimelineManager.
    * @param stationId    Optional label for the sounding plot. If empty, uses
    *                     "Lat, Lon" format.
    */
   void RequestSounding(double         lat,
                        double         lon,
                        const QString& model,
                        int            cycle,
                        int            forecastHour,
                        const QString& dateStr   = QString(),
                        const QString& stationId = QString());

signals:
   /**
    * @brief Emitted when a sounding image has been generated successfully.
    * @param imagePath Absolute path to the generated PNG file.
    */
   void SoundingImageReady(const QString& imagePath);

   /**
    * @brief Emitted when a sounding request fails.
    * @param message Human-readable error description.
    */
   void SoundingError(const QString& message);

private:
   std::unique_ptr<SoundingManagerImpl> p;
};

} // namespace scwx::qt::manager
