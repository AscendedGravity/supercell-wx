#include "wis_details_dialog.hpp"
#include "ui_wis_details_dialog.h"

#include <scwx/qt/manager/wis_manager.hpp>
#include <scwx/qt/ui/collapsible_group.hpp>
#include <scwx/qt/ui/wis_sparkline_widget.hpp>
#include <scwx/util/logger.hpp>

#include <algorithm>
#include <cmath>
#include <string>

#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QFont>
#include <QLabel>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace scwx::qt::ui
{

static const std::string logPrefix_ = "scwx::qt::ui::wis_details_dialog";
static const auto        logger_    = scwx::util::Logger::Create(logPrefix_);

static QColor ScoreColor(double value, double threshold)
{
   if (threshold <= 0.0)
   {
      return QColor(0x55, 0xff, 0x55);
   }
   if (value >= threshold)
   {
      return QColor(0xff, 0x55, 0x55);
   }
   if (value >= threshold * 0.75)
   {
      return QColor(0xff, 0xaa, 0x00);
   }
   if (value >= threshold * 0.5)
   {
      return QColor(0xff, 0xff, 0x55);
   }
   return QColor(0x55, 0xff, 0x55);
}

static QString ScoreColorStyle(double value, double threshold)
{
   return QString("color: %1; font-weight: bold;")
      .arg(ScoreColor(value, threshold).name());
}

static QLabel* MakeLabel(const QString& text,
                         const QString& style  = "",
                         bool           bold   = false,
                         QWidget*       parent = nullptr)
{
   auto* label = new QLabel(text, parent);
   if (!style.isEmpty())
   {
      label->setStyleSheet(style);
   }
   if (bold)
   {
      QFont f = label->font();
      f.setBold(true);
      label->setFont(f);
   }
   return label;
}

class WisDetailsDialog::Impl
{
public:
   explicit Impl(WisDetailsDialog* self) : self_(self) {}
   ~Impl() = default;

   void SetupUi()
   {
      auto* scrollLayout = self_->ui->scrollLayout;

      // -- Section 1: Current Conditions --
      currentConditionsGroup_ =
         new CollapsibleGroup(tr("Current Conditions"), self_);
      auto* ccGrid = new QGridLayout();
      ccGrid->setContentsMargins(4, 4, 4, 4);
      ccGrid->setSpacing(6);

      // Row 0: Current WIS and Threshold
      currentWisLabel_ = MakeLabel("--", "", true, self_);
      thresholdLabel_  = MakeLabel("--", "", true, self_);

      ccGrid->addWidget(MakeLabel(tr("Current WIS:"), "color: #888;"), 0, 0);
      ccGrid->addWidget(currentWisLabel_, 0, 1);
      ccGrid->addWidget(MakeLabel(tr("Threshold:"), "color: #888;"), 0, 2);
      ccGrid->addWidget(thresholdLabel_, 0, 3);

      // Row 1: 30m Ago, Now, 30m Ahead
      wis30mAgoLabel_     = MakeLabel("--", "", false, self_);
      wisNowLabel_        = MakeLabel("--", "", true, self_);
      wis30mFromNowLabel_ = MakeLabel("--", "", false, self_);

      ccGrid->addWidget(MakeLabel(tr("30m Ago:"), "color: #888;"), 1, 0);
      ccGrid->addWidget(wis30mAgoLabel_, 1, 1);
      ccGrid->addWidget(MakeLabel(tr("Now:"), "color: #888;"), 1, 2);
      ccGrid->addWidget(wisNowLabel_, 1, 3);
      ccGrid->addWidget(MakeLabel(tr("30m Ahead:"), "color: #888;"), 1, 4);
      ccGrid->addWidget(wis30mFromNowLabel_, 1, 5);

      // Row 2: Mode badge
      ccGrid->addWidget(MakeLabel(tr("Mode:"), "color: #888;"), 2, 0);
      modeBadgeLabel_ = MakeLabel("--", "font-weight: bold;", false, self_);
      ccGrid->addWidget(modeBadgeLabel_, 2, 1);

      qobject_cast<QVBoxLayout*>(currentConditionsGroup_->GetContentsLayout())
         ->addLayout(ccGrid);
      scrollLayout->addWidget(currentConditionsGroup_);

      // -- Section 2: 30-Minute Forecast --
      forecastGroup_   = new CollapsibleGroup(tr("Score History"), self_);
      sparklineWidget_ = new WisSparklineWidget(self_);
      forecastGroup_->GetContentsLayout()->addWidget(sparklineWidget_);

      // Legend
      auto* legendLabel = new QLabel(
         tr("\xe2\x96\xb2 Rising  \xe2\x96\xbc Falling  \xe2\x94\x80 Steady"),
         self_);
      legendLabel->setStyleSheet("color: #888; font-size: 10px;");
      legendLabel->setAlignment(Qt::AlignCenter);
      forecastGroup_->GetContentsLayout()->addWidget(legendLabel);

      scrollLayout->addWidget(forecastGroup_);

      // -- Section 3: Event Schedule --
      scheduleGroup_  = new CollapsibleGroup(tr("Event Schedule"), self_);
      auto* schedGrid = new QGridLayout();
      schedGrid->setContentsMargins(4, 4, 4, 4);
      schedGrid->setSpacing(6);

      schedGrid->addWidget(MakeLabel(tr("Event Start:"), "color: #888;"), 0, 0);
      eventStartLabel_ = MakeLabel("--", "", false, self_);
      schedGrid->addWidget(eventStartLabel_, 0, 1);

      schedGrid->addWidget(MakeLabel(tr("Peak:"), "color: #888;"), 1, 0);
      eventPeakLabel_ = MakeLabel("--", "", false, self_);
      schedGrid->addWidget(eventPeakLabel_, 1, 1);

      schedGrid->addWidget(
         MakeLabel(tr("Standby Until:"), "color: #888;"), 2, 0);
      standbyUntilLabel_ = MakeLabel("--", "", false, self_);
      schedGrid->addWidget(standbyUntilLabel_, 2, 1);

      schedGrid->addWidget(MakeLabel(tr("Event End:"), "color: #888;"), 3, 0);
      eventEndLabel_ = MakeLabel("--", "", false, self_);
      schedGrid->addWidget(eventEndLabel_, 3, 1);

      qobject_cast<QVBoxLayout*>(scheduleGroup_->GetContentsLayout())
         ->addLayout(schedGrid);
      scrollLayout->addWidget(scheduleGroup_);

      // -- Section 4: 7-Day Outlook --
      outlookGroup_ = new CollapsibleGroup(tr("7-Day Outlook"), self_);
      auto* outlookLayout =
         qobject_cast<QVBoxLayout*>(outlookGroup_->GetContentsLayout());
      if (outlookLayout == nullptr)
      {
         outlookLayout = new QVBoxLayout();
         outlookGroup_->SetContentsLayout(outlookLayout);
      }
      dayCardsLayout_ = new QHBoxLayout();
      dayCardsLayout_->setSpacing(4);
      outlookLayout->addLayout(dayCardsLayout_);

      // Create 7 day card placeholders
      for (int i = 0; i < 7; ++i)
      {
         auto* card = new QFrame(self_);
         card->setStyleSheet(
            "border: 1px solid palette(mid); border-radius: 6px; "
            "padding: 6px; margin: 2px;");
         auto* cardLayout = new QVBoxLayout(card);
         cardLayout->setSpacing(2);
         cardLayout->setContentsMargins(4, 4, 4, 4);

         auto* dayNameLabel = new QLabel("--", self_);
         QFont dayFont      = dayNameLabel->font();
         dayFont.setBold(true);
         dayFont.setPointSize(dayFont.pointSize() - 1);
         dayNameLabel->setFont(dayFont);
         dayNameLabel->setAlignment(Qt::AlignCenter);
         cardLayout->addWidget(dayNameLabel);

         auto* dateLabel = new QLabel("", self_);
         QFont dateFont  = dateLabel->font();
         dateFont.setPointSize(dateFont.pointSize() - 3);
         dateLabel->setFont(dateFont);
         dateLabel->setStyleSheet("color: #888;");
         dateLabel->setAlignment(Qt::AlignCenter);
         cardLayout->addWidget(dateLabel);

         auto* dosLabel = new QLabel("--", self_);
         QFont dosFont  = dosLabel->font();
         dosFont.setPointSize(dosFont.pointSize() + 4);
         dosFont.setBold(true);
         dosLabel->setFont(dosFont);
         dosLabel->setAlignment(Qt::AlignCenter);
         cardLayout->addWidget(dosLabel);

         auto* chanceLiveLabel = new QLabel("", self_);
         QFont chanceFont      = chanceLiveLabel->font();
         chanceFont.setPointSize(chanceFont.pointSize() - 2);
         chanceLiveLabel->setFont(chanceFont);
         chanceLiveLabel->setAlignment(Qt::AlignCenter);
         chanceLiveLabel->setStyleSheet("color: #888;");
         cardLayout->addWidget(chanceLiveLabel);

         auto* chanceVideoLabel = new QLabel("", self_);
         chanceVideoLabel->setFont(chanceFont);
         chanceVideoLabel->setAlignment(Qt::AlignCenter);
         chanceVideoLabel->setStyleSheet("color: #888;");
         cardLayout->addWidget(chanceVideoLabel);

         dayCardsLayout_->addWidget(card);

         dayCards_.push_back({dayNameLabel,
                              dateLabel,
                              dosLabel,
                              chanceLiveLabel,
                              chanceVideoLabel,
                              card});
      }

      scrollLayout->addWidget(outlookGroup_);

      // -- Section 5: Forecast Reasoning --
      reasoningGroup_ = new CollapsibleGroup(tr("Forecast Reasoning"), self_);
      reasoningText_  = new QTextBrowser(self_);
      reasoningText_->setReadOnly(true);
      reasoningText_->setFrameShape(QFrame::NoFrame);
      reasoningText_->setOpenExternalLinks(true);
      reasoningText_->setMinimumHeight(80);
      reasoningGroup_->GetContentsLayout()->addWidget(reasoningText_);
      scrollLayout->addWidget(reasoningGroup_);

      // Bottom spacer
      scrollLayout->addStretch();
   }

   void UpdateData()
   {
      auto wisManager = manager::WisManager::Instance();

      double      score     = wisManager->GetWeatherIntensityScore();
      double      threshold = wisManager->GetWeatherIntensityScoreThreshold();
      std::string mode      = wisManager->GetMode();

      double score30mAgo     = wisManager->GetWeatherIntensityScore30mAgo();
      double score30mFromNow = wisManager->GetWeatherIntensityScore30mFromNow();

      std::string eventStart   = wisManager->GetEventStart();
      std::string eventPeak    = wisManager->GetEventPeak();
      std::string standbyUntil = wisManager->GetStandbyUntil();
      std::string eventEnd     = wisManager->GetEventEnd();

      std::string forecastReasoning = wisManager->GetForecastReasoning();
      std::string timestamp         = wisManager->GetTimestamp();

      auto scoreHistory  = wisManager->GetScoreHistory();
      auto dailyOutlooks = wisManager->GetDailyOutlookScores();

      // Update current conditions
      currentWisLabel_->setText(QString::number(score, 'f', 2));
      currentWisLabel_->setStyleSheet(ScoreColorStyle(score, threshold));

      thresholdLabel_->setText(QString::number(threshold, 'f', 2));
      thresholdLabel_->setStyleSheet(ScoreColorStyle(threshold, threshold));

      wis30mAgoLabel_->setText(QString::number(score30mAgo, 'f', 2));
      wis30mAgoLabel_->setStyleSheet(ScoreColorStyle(score30mAgo, threshold));

      wisNowLabel_->setText(QString::number(score, 'f', 2));
      wisNowLabel_->setStyleSheet(ScoreColorStyle(score, threshold));

      wis30mFromNowLabel_->setText(QString::number(score30mFromNow, 'f', 2));
      wis30mFromNowLabel_->setStyleSheet(
         ScoreColorStyle(score30mFromNow, threshold));

      // Mode badge
      modeBadgeLabel_->setText(QString::fromStdString(mode));
      QString modeColor;
      if (mode == "live")
      {
         modeColor = "#55ff55";
      }
      else if (mode == "standby")
      {
         modeColor = "#ffaa00";
      }
      else
      {
         modeColor = "#888888";
      }
      modeBadgeLabel_->setStyleSheet(
         QString("color: %1; font-weight: bold;").arg(modeColor));

      // Update sparkline
      sparklineWidget_->SetData(scoreHistory);
      sparklineWidget_->SetThreshold(threshold);

      // Update event schedule
      eventStartLabel_->setText(QString::fromStdString(eventStart));
      eventPeakLabel_->setText(QString::fromStdString(eventPeak));
      standbyUntilLabel_->setText(QString::fromStdString(standbyUntil));
      eventEndLabel_->setText(QString::fromStdString(eventEnd));

      // Update daily outlook cards
      for (size_t i = 0; i < dayCards_.size(); ++i)
      {
         auto& [dayName,
                date,
                scoreLabel,
                chanceLiveLabel,
                chanceVideoLabel,
                card] = dayCards_[i];
         if (i < dailyOutlooks.size())
         {
            const auto& outlook = dailyOutlooks[i];
            dayName->setText(
               QString::fromStdString(outlook.day_name).left(3).toUpper());
            date->setText(QString::fromStdString(outlook.date));
            scoreLabel->setText(QString::number(outlook.dos_score, 'f', 0));
            scoreLabel->setStyleSheet(
               ScoreColorStyle(outlook.dos_score, outlook.threshold));
            chanceLiveLabel->setText(
               QString::fromStdString(outlook.chance_live));
            chanceVideoLabel->setText(
               QString::fromStdString(outlook.chance_video));
            card->setVisible(true);
         }
         else
         {
            dayName->setText("--");
            date->setText("");
            scoreLabel->setText("--");
            scoreLabel->setStyleSheet("");
            chanceLiveLabel->setText("");
            chanceVideoLabel->setText("");
            card->setVisible(false);
         }
      }

      // Update forecast reasoning
      if (!forecastReasoning.empty())
      {
         reasoningText_->setText(QString::fromStdString(forecastReasoning));
      }
      else
      {
         reasoningText_->setText(tr("No forecast reasoning available."));
      }

      // Update timestamp
      if (!timestamp.empty())
      {
         // Try to parse ISO 8601 timestamp
         QDateTime dt = QDateTime::fromString(QString::fromStdString(timestamp),
                                              Qt::ISODate);
         if (dt.isValid())
         {
            self_->ui->lastUpdatedLabel->setText(
               tr("Last updated: %1")
                  .arg(dt.toLocalTime().toString("yyyy-MM-dd hh:mm:ss")));
         }
         else
         {
            self_->ui->lastUpdatedLabel->setText(
               tr("Last updated: %1").arg(QString::fromStdString(timestamp)));
         }
      }
      else
      {
         self_->ui->lastUpdatedLabel->setText(tr("Last updated: --"));
      }
   }

   WisDetailsDialog* self_;
   CollapsibleGroup* currentConditionsGroup_ {nullptr};
   CollapsibleGroup* forecastGroup_ {nullptr};
   CollapsibleGroup* scheduleGroup_ {nullptr};
   CollapsibleGroup* outlookGroup_ {nullptr};
   CollapsibleGroup* reasoningGroup_ {nullptr};

   // Current conditions labels
   QLabel* currentWisLabel_ {nullptr};
   QLabel* thresholdLabel_ {nullptr};
   QLabel* wis30mAgoLabel_ {nullptr};
   QLabel* wisNowLabel_ {nullptr};
   QLabel* wis30mFromNowLabel_ {nullptr};
   QLabel* modeBadgeLabel_ {nullptr};

   // Sparkline
   WisSparklineWidget* sparklineWidget_ {nullptr};

   // Schedule labels
   QLabel* eventStartLabel_ {nullptr};
   QLabel* eventPeakLabel_ {nullptr};
   QLabel* standbyUntilLabel_ {nullptr};
   QLabel* eventEndLabel_ {nullptr};

   // 7-day outlook
   QHBoxLayout* dayCardsLayout_ {nullptr};
   struct DayCard
   {
      QLabel* dayNameLabel;
      QLabel* dateLabel;
      QLabel* dosLabel;
      QLabel* chanceLiveLabel;
      QLabel* chanceVideoLabel;
      QFrame* card;
   };
   std::vector<DayCard> dayCards_ {};

   // Reasoning
   QTextBrowser* reasoningText_ {nullptr};
};

WisDetailsDialog::WisDetailsDialog(QWidget* parent) :
    QDialog(parent),
    p(std::make_unique<Impl>(this)),
    ui(new Ui::WisDetailsDialog)
{
   ui->setupUi(this);

   setWindowTitle(tr("Weather Intensity Score"));
   setAttribute(Qt::WA_DeleteOnClose, false);

   p->SetupUi();
}

WisDetailsDialog::~WisDetailsDialog()
{
   delete ui;
}

void WisDetailsDialog::UpdateData()
{
   p->UpdateData();
}

} // namespace scwx::qt::ui
