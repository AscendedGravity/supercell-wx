#include <scwx/qt/ui/sounding_dialog.hpp>
#include <scwx/qt/manager/sounding_manager.hpp>
#include <scwx/util/logger.hpp>

#include <string>

#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

namespace scwx::qt::ui
{

static const std::string logPrefix_ = "scwx::qt::ui::sounding_dialog";
static const auto        logger_    = util::Logger::Create(logPrefix_);

/**
 * @brief A QLabel that auto-scales its pixmap to fit the available space
 *        while maintaining aspect ratio.
 */
class SoundingImageLabel : public QLabel
{
public:
   SoundingImageLabel(QWidget* parent = nullptr) : QLabel(parent)
   {
      setAlignment(Qt::AlignCenter);
      setMinimumSize(400, 400);
      setStyleSheet(
         QStringLiteral("background-color: #1a1a1a; "
                        "color: #888; "
                        "padding: 20px;"));
   }

   void SetFullPixmap(const QPixmap& pixmap)
   {
      fullPixmap_ = pixmap;
      ScaleToFit();
   }

   void ClearFullPixmap() { fullPixmap_ = QPixmap(); }

protected:
   void resizeEvent(QResizeEvent* event) override
   {
      QLabel::resizeEvent(event);
      ScaleToFit();
   }

private:
   void ScaleToFit()
   {
      if (fullPixmap_.isNull())
      {
         return;
      }

      QPixmap scaled = fullPixmap_.scaled(
         size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
      QLabel::setPixmap(scaled);
   }

   QPixmap fullPixmap_ {};
};

class SoundingDialogImpl
{
public:
   explicit SoundingDialogImpl(SoundingDialog* self) : self_(self) {}
   ~SoundingDialogImpl() = default;

   SoundingDialogImpl(const SoundingDialogImpl&)            = delete;
   SoundingDialogImpl& operator=(const SoundingDialogImpl&) = delete;
   SoundingDialogImpl(SoundingDialogImpl&&)                 = delete;
   SoundingDialogImpl& operator=(SoundingDialogImpl&&)      = delete;

   void SetupUi(QWidget* widget)
   {
      static constexpr double kMaxLatitude      = 90.0;
      static constexpr double kMaxLongitude     = 180.0;
      static constexpr double kDefaultLatitude  = 35.0;
      static constexpr double kDefaultLongitude = -97.0;
      static constexpr double kCoordinateStep   = 0.1;
      static constexpr int    kMaxForecastHour  = 384;
      static constexpr int    kForecastStep     = 3;

      static constexpr int kMargin         = 8;
      static constexpr int kSpacing        = 4;
      static constexpr int kControlSpacing = 6;

      auto* mainLayout = new QVBoxLayout(widget);
      mainLayout->setContentsMargins(kMargin, kMargin, kMargin, kMargin);
      mainLayout->setSpacing(kSpacing);

      // Controls area
      auto* controlsWidget = new QWidget();
      auto* controlsLayout = new QVBoxLayout(controlsWidget);
      controlsLayout->setContentsMargins(0, 0, 0, 0);
      controlsLayout->setSpacing(kSpacing);

      // Row 1: Location
      auto* row1Layout = new QHBoxLayout();
      row1Layout->setSpacing(kControlSpacing);

      auto* latLabel = new QLabel(QStringLiteral("Lat:"));
      row1Layout->addWidget(latLabel);
      latSpinBox_ = new QDoubleSpinBox();
      latSpinBox_->setRange(-kMaxLatitude, kMaxLatitude);
      latSpinBox_->setDecimals(4);
      latSpinBox_->setValue(kDefaultLatitude);
      latSpinBox_->setSingleStep(kCoordinateStep);
      static constexpr int kSpinBoxWidth = 80;
      latSpinBox_->setMinimumWidth(kSpinBoxWidth);
      row1Layout->addWidget(latSpinBox_);

      auto* lonLabel = new QLabel(QStringLiteral("Lon:"));
      row1Layout->addWidget(lonLabel);
      lonSpinBox_ = new QDoubleSpinBox();
      lonSpinBox_->setRange(-kMaxLongitude, kMaxLongitude);
      lonSpinBox_->setDecimals(4);
      lonSpinBox_->setValue(kDefaultLongitude);
      lonSpinBox_->setSingleStep(kCoordinateStep);
      lonSpinBox_->setMinimumWidth(kSpinBoxWidth);
      row1Layout->addWidget(lonSpinBox_);

      controlsLayout->addLayout(row1Layout);

      // Row 2: Model, Cycle, Forecast Hour, and Fetch
      auto* row2Layout = new QHBoxLayout();
      row2Layout->setSpacing(kControlSpacing);

      // Model selector
      row2Layout->addWidget(new QLabel(QStringLiteral("Model:")));
      modelCombo_ = new QComboBox();
      modelCombo_->addItem(QStringLiteral("HRRR"), QStringLiteral("hrrr"));
      modelCombo_->addItem(QStringLiteral("GFS"), QStringLiteral("gfs"));
      modelCombo_->addItem(QStringLiteral("RAP"), QStringLiteral("rap"));
      modelCombo_->addItem(QStringLiteral("NAM"), QStringLiteral("nam"));
      modelCombo_->addItem(QStringLiteral("ECMWF"),
                           QStringLiteral("ecmwf-open-data"));
      modelCombo_->addItem(QStringLiteral("RRFS-A"), QStringLiteral("rrfs-a"));
      row2Layout->addWidget(modelCombo_);

      row2Layout->addWidget(new QLabel(QStringLiteral("Cycle:")));
      cycleCombo_ = new QComboBox();
      cycleCombo_->addItem(QStringLiteral("00Z"), 0);
      cycleCombo_->addItem(QStringLiteral("06Z"), 6);
      cycleCombo_->addItem(QStringLiteral("12Z"), 12);
      cycleCombo_->addItem(QStringLiteral("18Z"), 18);

      // Auto-select the most recent cycle that has had time to propagate
      {
         static constexpr int kDataLagHours = 2;
         static constexpr int kCycleHours[] = {0, 6, 12, 18};
         static constexpr int kNumCycles    = 4;
         int currentHour = QDateTime::currentDateTimeUtc().time().hour();

         int bestIdx = 3; // default to 18Z
         for (int i = 0; i < kNumCycles; ++i)
         {
            if (kCycleHours[i] <= currentHour - kDataLagHours)
            {
               bestIdx = i;
            }
         }
         cycleCombo_->setCurrentIndex(bestIdx);
      }

      row2Layout->addWidget(cycleCombo_);

      row2Layout->addWidget(new QLabel(QStringLiteral("Fhr:")));
      fhrCombo_ = new QComboBox();
      for (int f = 0; f <= kMaxForecastHour; f += kForecastStep)
      {
         fhrCombo_->addItem(
            QStringLiteral("F%1").arg(f, 3, 10, QLatin1Char('0')), f);
      }
      fhrCombo_->setCurrentIndex(0);
      row2Layout->addWidget(fhrCombo_);

      fetchButton_ = new QPushButton(QStringLiteral("Fetch Sounding"));
      fetchButton_->setStyleSheet(QStringLiteral("font-weight: bold;"));
      static constexpr int kStretchFetch = 1;
      row2Layout->addWidget(fetchButton_, kStretchFetch);

      controlsLayout->addLayout(row2Layout);
      mainLayout->addWidget(controlsWidget);

      // Separator
      auto* separator = new QFrame();
      separator->setFrameShape(QFrame::HLine);
      separator->setFrameShadow(QFrame::Sunken);
      separator->setStyleSheet(QStringLiteral("background-color: #555;"));
      mainLayout->addWidget(separator);

      // Image area — auto-scales to fill available space
      imageLabel_ = new SoundingImageLabel();
      imageLabel_->setText(
         QStringLiteral("No sounding loaded.\n"
                        "Select a location and click "
                        "\"Fetch Sounding\"."));
      mainLayout->addWidget(imageLabel_, 1);

      // Connect signals
      QObject::connect(fetchButton_,
                       &QPushButton::clicked,
                       self_,
                       &SoundingDialog::OnFetchClicked);

      QObject::connect(&manager::SoundingManager::Instance(),
                       &manager::SoundingManager::SoundingImageReady,
                       self_,
                       &SoundingDialog::OnSoundingImageReady);

      QObject::connect(&manager::SoundingManager::Instance(),
                       &manager::SoundingManager::SoundingError,
                       self_,
                       &SoundingDialog::OnSoundingError);
   }

   SoundingDialog*     self_;
   SoundingImageLabel* imageLabel_ {nullptr};
   QDoubleSpinBox*     latSpinBox_ {nullptr};
   QDoubleSpinBox*     lonSpinBox_ {nullptr};
   QComboBox*          modelCombo_ {nullptr};
   QComboBox*          cycleCombo_ {nullptr};
   QComboBox*          fhrCombo_ {nullptr};
   QPushButton*        fetchButton_ {nullptr};
};

SoundingDialog::SoundingDialog(QWidget* parent) :
    QDialog(parent, Qt::Window), p(std::make_unique<SoundingDialogImpl>(this))
{
   setWindowTitle(QStringLiteral("Sounding"));
   setMinimumSize(500, 600);
   p->SetupUi(this);
}
SoundingDialog::~SoundingDialog() = default;

void SoundingDialog::SetLocation(double lat, double lon)
{
   p->latSpinBox_->setValue(lat);
   p->lonSpinBox_->setValue(lon);
}

void SoundingDialog::OnSoundingImageReady(const QString& imagePath)
{
   QPixmap pixmap(imagePath);
   if (pixmap.isNull())
   {
      p->imageLabel_->setText(
         QStringLiteral("Failed to load sounding image:\n%1").arg(imagePath));
      p->imageLabel_->ClearFullPixmap();
   }
   else
   {
      p->imageLabel_->SetFullPixmap(pixmap);
   }

   p->fetchButton_->setEnabled(true);
   p->fetchButton_->setText(QStringLiteral("Fetch Sounding"));
}

void SoundingDialog::OnSoundingError(const QString& message)
{
   p->imageLabel_->ClearFullPixmap();
   p->imageLabel_->setText(QStringLiteral("Sounding Error:\n%1").arg(message));
   QMessageBox::warning(this, QStringLiteral("Sounding Error"), message);
   p->fetchButton_->setEnabled(true);
   p->fetchButton_->setText(QStringLiteral("Fetch Sounding"));
}

void SoundingDialog::OnFetchClicked()
{
   double  lat   = p->latSpinBox_->value();
   double  lon   = p->lonSpinBox_->value();
   QString model = p->modelCombo_->currentData().toString();
   int     cycle = p->cycleCombo_->currentData().toInt();
   int     fhr   = p->fhrCombo_->currentData().toInt();

   p->fetchButton_->setEnabled(false);
   p->fetchButton_->setText(QStringLiteral("Fetching..."));

   p->imageLabel_->ClearFullPixmap();
   p->imageLabel_->setText(QStringLiteral("Fetching sounding...\n"
                                          "Model: %1, Cycle: %2Z, F%3\n"
                                          "Lat: %4, Lon: %5")
                              .arg(p->modelCombo_->currentText())
                              .arg(cycle, 2, 10, QLatin1Char('0'))
                              .arg(fhr, 3, 10, QLatin1Char('0'))
                              .arg(lat, 0, 'f', 4)
                              .arg(lon, 0, 'f', 4));

   manager::SoundingManager::Instance().RequestSounding(
      lat, lon, model, cycle, fhr);
}

} // namespace scwx::qt::ui
