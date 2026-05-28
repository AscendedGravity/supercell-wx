#include "watch_directory_dialog.hpp"
#include "ui_watch_directory_dialog.h"

#include <scwx/qt/manager/radar_product_manager.hpp>
#include <scwx/util/logger.hpp>

#include <QDir>
#include <QFileDialog>
#include <QFileSystemWatcher>
#include <QPushButton>
#include <QStandardPaths>

namespace scwx
{
namespace qt
{
namespace ui
{

static const std::string logPrefix_ = "scwx::qt::ui::watch_directory_dialog";

class WatchDirectoryDialogImpl
{
public:
   explicit WatchDirectoryDialogImpl() = default;
   ~WatchDirectoryDialogImpl()         = default;

   QFileSystemWatcher* watcher_ {nullptr};
   std::string         watchedPath_;
};

WatchDirectoryDialog::WatchDirectoryDialog(QWidget* parent) :
    QDialog(parent),
    p {std::make_unique<WatchDirectoryDialogImpl>()},
    ui(new Ui::WatchDirectoryDialog)
{
   ui->setupUi(this);

   p->watcher_ = new QFileSystemWatcher(this);

   // Browse button
   connect(
      ui->browseButton,
      &QPushButton::clicked,
      [this]()
      {
         QString dir = QFileDialog::getExistingDirectory(
            this,
            "Select Directory to Watch",
            QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
         if (!dir.isEmpty())
         {
            ui->directoryEdit->setText(dir);
         }
      });

   // Start/Stop watching button
   connect(ui->watchButton,
           &QPushButton::clicked,
           [this]()
           {
              if (p->watchedPath_.empty())
              {
                 // Start watching
                 QString path = ui->directoryEdit->text();
                 if (path.isEmpty())
                 {
                    return;
                 }

                 p->watchedPath_ = path.toStdString();
                 p->watcher_->addPath(path);
                 ui->watchButton->setText("Stop Watching");
                 ui->directoryEdit->setEnabled(false);
                 ui->browseButton->setEnabled(false);
                 ui->statusLabel->setText("Watching...");

                 auto logger = scwx::util::Logger::Create(logPrefix_);
                 logger->info("Started watching: {}", p->watchedPath_);
              }
              else
              {
                 // Stop watching
                 p->watcher_->removePaths(p->watcher_->directories());
                 p->watchedPath_.clear();
                 ui->watchButton->setText("Start Watching");
                 ui->directoryEdit->setEnabled(true);
                 ui->browseButton->setEnabled(true);
                 ui->statusLabel->setText("Stopped");

                 auto logger = scwx::util::Logger::Create(logPrefix_);
                 logger->info("Stopped watching");
              }
           });

   // File system watcher -- when a new file appears
   connect(
      p->watcher_,
      &QFileSystemWatcher::directoryChanged,
      [this](const QString& path)
      {
         auto logger = scwx::util::Logger::Create(logPrefix_);

         QDir dir(path);
         auto entries = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot,
                                          QDir::Time); // newest first
         if (!entries.isEmpty())
         {
            auto&   latest   = entries.first();
            QString filename = latest.absoluteFilePath();

            // Only process NEXRAD files
            QString base = latest.fileName();
            if (base.endsWith(".V06", Qt::CaseInsensitive) ||
                base.contains("SDUS", Qt::CaseInsensitive) ||
                base.endsWith(".gz", Qt::CaseInsensitive))
            {
               logger->info("New file detected: {}", filename.toStdString());
               ui->statusLabel->setText(
                  QString("Loading: %1").arg(latest.fileName()));
               ui->lastFileLabel->setText(latest.fileName());

               scwx::qt::manager::RadarProductManager::LoadFile(
                  filename.toStdString());
            }
         }
      });

   // Close button
   connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

WatchDirectoryDialog::~WatchDirectoryDialog()
{
   delete ui;
}

} // namespace ui
} // namespace qt
} // namespace scwx
