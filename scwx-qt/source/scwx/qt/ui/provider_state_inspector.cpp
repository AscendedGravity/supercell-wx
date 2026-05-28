#include "provider_state_inspector.hpp"
#include "ui_provider_state_inspector.h"

#include <scwx/qt/manager/radar_product_manager.hpp>
#include <scwx/util/logger.hpp>
#include <scwx/util/time.hpp>

#include <spdlog/spdlog.h>

#include <QHeaderView>
#include <QTimer>
#include <QTableWidget>
#include <QComboBox>
#include <QPushButton>

namespace scwx
{
namespace qt
{
namespace ui
{

static const std::string logPrefix_ = "scwx::qt::ui::provider_state_inspector";

class ProviderStateInspectorImpl
{
public:
   explicit ProviderStateInspectorImpl() = default;
   ~ProviderStateInspectorImpl()         = default;

   QTimer* refreshTimer_ {nullptr};
};

ProviderStateInspector::ProviderStateInspector(QWidget* parent) :
    QDialog(parent),
    p {std::make_unique<ProviderStateInspectorImpl>()},
    ui(new Ui::ProviderStateInspector)
{
   ui->setupUi(this);

   // Configure table
   ui->providerTable->setColumnCount(8);
   ui->providerTable->setHorizontalHeaderLabels({"Radar Site",
                                                 "Provider",
                                                 "Group",
                                                 "Product",
                                                 "Refresh",
                                                 "Cache",
                                                 "Period",
                                                 "Count"});
   ui->providerTable->horizontalHeader()->setStretchLastSection(true);
   ui->providerTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
   ui->providerTable->setSelectionBehavior(QAbstractItemView::SelectRows);

   // Connect log level combo
   connect(ui->logLevelCombo,
           QOverload<int>::of(&QComboBox::currentIndexChanged),
           [](int index)
           {
              spdlog::level::level_enum level;
              switch (index)
              {
              case 0:
                 level = spdlog::level::trace;
                 break;
              case 1:
                 level = spdlog::level::debug;
                 break;
              case 2:
              default:
                 level = spdlog::level::info;
                 break;
              case 3:
                 level = spdlog::level::warn;
                 break;
              }
              spdlog::set_level(level);

              auto logger = scwx::util::Logger::Create(logPrefix_);
              logger->info("Log level set to {}",
                           spdlog::level::to_string_view(level));
           });

   // Refresh timer
   p->refreshTimer_ = new QTimer(this);
   connect(
      p->refreshTimer_,
      &QTimer::timeout,
      [this]()
      {
         auto infos =
            scwx::qt::manager::RadarProductManager::GetProviderDebugInfo();

         ui->providerTable->setRowCount(static_cast<int>(infos.size()));
         for (int i = 0; i < static_cast<int>(infos.size()); ++i)
         {
            auto& info = infos[i];
            ui->providerTable->setItem(
               i,
               0,
               new QTableWidgetItem(QString::fromStdString(info.radarSite)));
            ui->providerTable->setItem(
               i,
               1,
               new QTableWidgetItem(QString::fromStdString(info.providerName)));
            ui->providerTable->setItem(
               i,
               2,
               new QTableWidgetItem(
                  info.group == common::RadarProductGroup::Level2 ? "L2" :
                                                                    "L3"));
            ui->providerTable->setItem(
               i,
               3,
               new QTableWidgetItem(QString::fromStdString(info.product)));
            ui->providerTable->setItem(
               i, 4, new QTableWidgetItem(info.refreshEnabled ? "ON" : "OFF"));
            ui->providerTable->setItem(
               i, 5, new QTableWidgetItem(QString::number(info.cacheSize)));
            ui->providerTable->setItem(
               i,
               6,
               new QTableWidgetItem(QString::number(info.updatePeriod.count()) +
                                    "s"));
            ui->providerTable->setItem(
               i, 7, new QTableWidgetItem(QString::number(info.refreshCount)));
         }
      });
   p->refreshTimer_->start(2000);

   // Force Refresh button
   connect(ui->forceRefreshButton,
           &QPushButton::clicked,
           []() { scwx::qt::manager::RadarProductManager::ForceRefresh(); });

   // Clear Cache button
   connect(ui->clearCacheButton,
           &QPushButton::clicked,
           []()
           { scwx::qt::manager::RadarProductManager::ClearProviderCache(); });
}

ProviderStateInspector::~ProviderStateInspector()
{
   delete ui;
}

} // namespace ui
} // namespace qt
} // namespace scwx
