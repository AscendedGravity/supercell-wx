#include <scwx/qt/manager/sounding_manager.hpp>
#include <scwx/qt/manager/timeline_manager.hpp>
#include <scwx/util/logger.hpp>
#include <scwx/util/time.hpp>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <map>
#include <sstream>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

#include <QStorageInfo>

namespace scwx::qt::manager
{

static const std::string logPrefix_ = "scwx::qt::manager::sounding_manager";
static const auto        logger_    = util::Logger::Create(logPrefix_);

static constexpr int kProcessTimeoutMs =
   600'000; // 10 minutes (RRFS-A can be ~3GB)

class SoundingManagerImpl
{
public:
   explicit SoundingManagerImpl(SoundingManager* self) : self_(self) {}
   ~SoundingManagerImpl() = default;

   SoundingManagerImpl(const SoundingManagerImpl&)            = delete;
   SoundingManagerImpl& operator=(const SoundingManagerImpl&) = delete;
   SoundingManagerImpl(SoundingManagerImpl&&)                 = delete;
   SoundingManagerImpl& operator=(SoundingManagerImpl&&)      = delete;

   /**
    * @brief Build a cache key from sounding request parameters.
    */
   static QString BuildCacheKey(const QString& model,
                                const QString& dateStr,
                                int            cycle,
                                int            forecastHour,
                                double         lat,
                                double         lon)
   {
      return QString("%1/%2/%3z/f%4/%5_%6")
         .arg(model,
              dateStr,
              QString::number(cycle).rightJustified(2, '0'),
              QString::number(forecastHour).rightJustified(3, '0'),
              QString::number(lat, 'f', 2),
              QString::number(lon, 'f', 2));
   }

   /**
    * @brief Get the date as YYYYMMDD from TimelineManager, falling back to
    *        current UTC date if the timeline has not been initialized yet.
    */
   static QString GetDateFromTimeline()
   {
      constexpr std::time_t kMinValidTime =
         static_cast<std::time_t>(946684800); // 2000-01-01

      auto selectedTime = TimelineManager::Instance()->GetSelectedTime();
      auto timeT        = std::chrono::system_clock::to_time_t(selectedTime);

      // If the timeline has never been set, it defaults to epoch (1970-01-01).
      // Use the current UTC time instead.
      if (timeT < kMinValidTime)
      {
         timeT = std::time(nullptr);
      }

      // Convert to calendar time (cross-platform safe)
      std::tm tm {};
#ifdef _MSC_VER
      gmtime_s(&tm, &timeT);
#else
      gmtime_r(&timeT, &tm);
#endif

      std::ostringstream ss;
      ss << std::put_time(&tm, "%Y%m%d");
      return QString::fromStdString(ss.str());
   }

   void RequestSounding(SoundingManager* self,
                        double           lat,
                        double           lon,
                        const QString&   model,
                        int              cycle,
                        int              forecastHour,
                        const QString&   dateStr,
                        const QString&   stationId)
   {
      // Resolve date
      QString resolvedDate =
         dateStr.isEmpty() ? GetDateFromTimeline() : dateStr;

      // Resolve station ID
      QString resolvedStationId =
         stationId.isEmpty() ?
            QString("%1, %2").arg(QString::number(lat, 'f', 2),
                                  QString::number(lon, 'f', 2)) :
            stationId;

      logger_->info(
         "Sounding requested: lat={}, lon={}, model={}, "
         "date={}, cycle={}, fhr={}",
         lat,
         lon,
         model.toStdString(),
         resolvedDate.toStdString(),
         cycle,
         forecastHour);

      // Check cache
      QString cacheKey =
         BuildCacheKey(model, resolvedDate, cycle, forecastHour, lat, lon);
      auto it = cache_.find(cacheKey);
      if (it != cache_.end())
      {
         logger_->info("Sounding cache hit: {}", cacheKey.toStdString());
         Q_EMIT self->SoundingImageReady(it.value());
         return;
      }

      // Find the binary
      QString binaryPath = SoundingManager::FindBinary();
      if (binaryPath.isEmpty())
      {
         Q_EMIT self->SoundingError(
            "sounding_plot binary not found. "
            "Place it next to the application executable or add it to PATH.");
         return;
      }

      // Create output directory
      QString outputDir =
         QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
         QStringLiteral("/supercell-wx/soundings/") + cacheKey;
      QDir().mkpath(outputDir);

      // Map model IDs to their most reliable data source
      static const QHash<QString, QString> kModelSource = {
         {QStringLiteral("hrrr"), QStringLiteral("aws")},
         {QStringLiteral("rap"), QStringLiteral("aws")},
         {QStringLiteral("gfs"), QStringLiteral("nomads")},
         {QStringLiteral("nam"), QStringLiteral("nomads")},
         {QStringLiteral("ecmwf-open-data"), QStringLiteral("ecmwf")},
         {QStringLiteral("rrfs-a"), QStringLiteral("aws")}};

      QString source = kModelSource.value(model, QStringLiteral("aws"));

      // Build command-line arguments
      QStringList args;
      args << QStringLiteral("--model") << model << QStringLiteral("--date")
           << resolvedDate << QStringLiteral("--cycle")
           << QString::number(cycle) << QStringLiteral("--forecast-hour")
           << QString::number(forecastHour) << QStringLiteral("--source")
           << source << QStringLiteral("--lat") << QString::number(lat, 'f', 6)
           << QStringLiteral("--lon") << QString::number(lon, 'f', 6)
           << QStringLiteral("--station-id") << resolvedStationId
           << QStringLiteral("--out-dir") << outputDir
           << QStringLiteral("--sample-method") << QStringLiteral("nearest");

      logger_->info("Running: {} {}",
                    binaryPath.toStdString(),
                    args.join(' ').toStdString());

      // Launch the process asynchronously
      auto* process = new QProcess(self);
      process->setProcessChannelMode(QProcess::MergedChannels);

      QObject::connect(
         process,
         QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
         self,
         [this, self, process, outputDir, cacheKey](int exitCode,
                                                    QProcess::ExitStatus status)
         {
            process->deleteLater();

            if (status == QProcess::NormalExit && exitCode == 0)
            {
               // Find the generated PNG file in the output directory
               QDir        dir(outputDir);
               QStringList filters {QStringLiteral("*.png")};
               QStringList files = dir.entryList(filters, QDir::Files);

               if (!files.isEmpty())
               {
                  QString imagePath = dir.absoluteFilePath(files.first());
                  logger_->info("Sounding image generated: {}",
                                imagePath.toStdString());

                  cache_[cacheKey] = imagePath;
                  Q_EMIT self->SoundingImageReady(imagePath);

                  // Trim older cache entries after each new fetch
                  SoundingManager::PruneCache();
               }
               else
               {
                  QString stderrLog =
                     QString::fromUtf8(process->readAllStandardOutput());
                  logger_->warn(
                     "sounding_plot completed but no PNG found. Output:\n{}",
                     stderrLog.toStdString());
                  Q_EMIT self->SoundingError(
                     "sounding_plot completed but no PNG file was generated.");
               }
            }
            else
            {
               QString output =
                  QString::fromUtf8(process->readAllStandardOutput());
               logger_->warn("sounding_plot failed (exit {}):\n{}",
                             exitCode,
                             output.toStdString());
               Q_EMIT self->SoundingError(
                  QStringLiteral("sounding_plot failed (exit %1): %2")
                     .arg(exitCode)
                     .arg(output));
            }
         });

      // Timeout safeguard
      QTimer::singleShot(kProcessTimeoutMs,
                         process,
                         [process]()
                         {
                            if (process->state() != QProcess::NotRunning)
                            {
                               process->kill();
                            }
                         });

      process->start(binaryPath, args);
   }

   SoundingManager*        self_;
   QHash<QString, QString> cache_ {};
   static QString          sBinaryPath_;
};

QString SoundingManagerImpl::sBinaryPath_ = QString();

SoundingManager::SoundingManager() :
    QObject(nullptr), p(std::make_unique<SoundingManagerImpl>(this))
{
   // Prune stale cache entries on startup
   PruneCache();
}
SoundingManager::~SoundingManager() = default;

SoundingManager& SoundingManager::Instance()
{
   static SoundingManager instance;
   return instance;
}

QString SoundingManager::FindBinary()
{
   // 1. Use configured path if set
   if (!SoundingManagerImpl::sBinaryPath_.isEmpty())
   {
      if (QFile::exists(SoundingManagerImpl::sBinaryPath_))
      {
         return SoundingManagerImpl::sBinaryPath_;
      }
   }

   // 2. Check next to the application executable
   QString appDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
   QString binaryName = QStringLiteral("sounding_plot.exe");
#else
   QString binaryName = QStringLiteral("sounding_plot");
#endif
   QString binaryPath = appDir + QStringLiteral("/") + binaryName;
   if (QFile::exists(binaryPath))
   {
      return QFileInfo(binaryPath).absoluteFilePath();
   }

   // 3. Search PATH
   binaryPath = QStandardPaths::findExecutable(binaryName);
   if (!binaryPath.isEmpty())
   {
      return binaryPath;
   }

   logger_->warn("sounding_plot binary not found in PATH or application dir");
   return QString();
}

void SoundingManager::SetBinaryPath(const QString& path)
{
   SoundingManagerImpl::sBinaryPath_ = path;
}

void SoundingManager::RequestSounding(double         lat,
                                      double         lon,
                                      const QString& model,
                                      int            cycle,
                                      int            forecastHour,
                                      const QString& dateStr,
                                      const QString& stationId)
{
   p->RequestSounding(
      this, lat, lon, model, cycle, forecastHour, dateStr, stationId);
}

void SoundingManager::PruneCache(std::size_t keepMostRecent)
{
   QString cacheDir =
      QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
      QStringLiteral("/supercell-wx/soundings");

   QDir dir(cacheDir);
   if (!dir.exists())
   {
      return;
   }

   // Collect subdirectories with their last-modified time
   using DirEntry = std::pair<QDateTime, QFileInfo>;
   std::vector<DirEntry> entries;

   QDirIterator it(cacheDir, QDir::Dirs | QDir::NoDotAndDotDot);
   while (it.hasNext())
   {
      QFileInfo info(it.next());
      entries.emplace_back(info.lastModified(), info);
   }

   if (entries.size() <= keepMostRecent)
   {
      return;
   }

   // Sort oldest-first (compare only the timestamp)
   std::sort(entries.begin(),
             entries.end(),
             [](const DirEntry& a, const DirEntry& b)
             { return a.first < b.first; });

   // Remove the oldest entries beyond keepMostRecent
   std::size_t toRemove = entries.size() - keepMostRecent;
   for (std::size_t i = 0; i < toRemove; ++i)
   {
      const QString path = entries[i].second.absoluteFilePath();
      logger_->info("Removing stale sounding cache: {}", path.toStdString());
      QDir(path).removeRecursively();
   }
}

void SoundingManager::ClearRustwxCache()
{
   // rustwx stores downloaded GRIB data under the user home cache dir
#ifdef Q_OS_WIN
   QString cacheDir =
      QStandardPaths::writableLocation(QStandardPaths::HomeLocation) +
      QStringLiteral("/.cache/rustwx");
#else
   QString cacheDir = QDir::homePath() + QStringLiteral("/.cache/rustwx");
#endif

   QDir dir(cacheDir);
   if (!dir.exists())
   {
      logger_->info("No rustwx cache to clear");
      return;
   }

   qint64       totalSize = 0;
   QDirIterator szIt(cacheDir,
                     QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                     QDirIterator::Subdirectories);
   while (szIt.hasNext())
   {
      totalSize += QFileInfo(szIt.next()).size();
   }

   bool ok = dir.removeRecursively();
   if (ok)
   {
      logger_->info("Cleared rustwx cache ({} MB freed)",
                    totalSize / (1024 * 1024));
   }
   else
   {
      logger_->warn("Failed to clear rustwx cache at {}",
                    cacheDir.toStdString());
   }
}

} // namespace scwx::qt::manager
