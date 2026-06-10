#include <scwx/qt/ui/satellite_products_widget.hpp>
#include <scwx/qt/ui/flow_layout.hpp>
#include <scwx/util/logger.hpp>

#include <execution>

#include <QToolButton>
#include <QVBoxLayout>
#include <QLabel>

namespace scwx
{
namespace qt
{
namespace ui
{

static const std::string logPrefix_ = "scwx::qt::ui::satellite_products_widget";
static const auto        logger_    = util::Logger::Create(logPrefix_);

class SatelliteProductsWidgetImpl : public QObject
{
   Q_OBJECT

public:
   explicit SatelliteProductsWidgetImpl(SatelliteProductsWidget* self) :
       self_ {self}, layout_ {new QVBoxLayout(self)}, productButtons_ {}
   {
      layout_->setContentsMargins(0, 0, 0, 0);

      // Group 1: VIS
      QLabel* visLabel = new QLabel(tr("Visible (VIS)"));
      visLabel->setStyleSheet(
         "font-weight: bold; color: #888888; font-size: 10px; text-transform: "
         "uppercase; margin-top: 6px; margin-bottom: 2px;");
      layout_->addWidget(visLabel);
      QWidget*        visWidget = new QWidget();
      ui::FlowLayout* visLayout = new ui::FlowLayout(visWidget);
      visLayout->setContentsMargins(0, 0, 0, 0);
      layout_->addWidget(visWidget);

      // Group 2: NIR
      QLabel* nirLabel = new QLabel(tr("Near-Infrared (NIR)"));
      nirLabel->setStyleSheet(
         "font-weight: bold; color: #888888; font-size: 10px; text-transform: "
         "uppercase; margin-top: 6px; margin-bottom: 2px;");
      layout_->addWidget(nirLabel);
      QWidget*        nirWidget = new QWidget();
      ui::FlowLayout* nirLayout = new ui::FlowLayout(nirWidget);
      nirLayout->setContentsMargins(0, 0, 0, 0);
      layout_->addWidget(nirWidget);

      // Group 3: IR
      QLabel* irLabel = new QLabel(tr("Infrared (IR)"));
      irLabel->setStyleSheet(
         "font-weight: bold; color: #888888; font-size: 10px; text-transform: "
         "uppercase; margin-top: 6px; margin-bottom: 2px;");
      layout_->addWidget(irLabel);
      QWidget*        irWidget = new QWidget();
      ui::FlowLayout* irLayout = new ui::FlowLayout(irWidget);
      irLayout->setContentsMargins(0, 0, 0, 0);
      layout_->addWidget(irWidget);

      for (common::SatelliteBand band : common::SatelliteBandIterator())
      {
         QToolButton* toolButton = new QToolButton();
         toolButton->setText(
            QString::fromStdString(common::GetSatelliteBandName(band)));
         toolButton->setStatusTip(
            tr(common::GetSatelliteBandDescription(band).c_str()));
         toolButton->setToolTip(
            tr(common::GetSatelliteBandDescription(band).c_str()));

         if (band == common::SatelliteBand::Band01 ||
             band == common::SatelliteBand::Band02)
         {
            visLayout->addWidget(toolButton);
         }
         else if (band >= common::SatelliteBand::Band03 &&
                  band <= common::SatelliteBand::Band06)
         {
            nirLayout->addWidget(toolButton);
         }
         else if (band >= common::SatelliteBand::Band07 &&
                  band <= common::SatelliteBand::Band16)
         {
            irLayout->addWidget(toolButton);
         }

         productButtons_.push_back(toolButton);

         QObject::connect(toolButton,
                          &QToolButton::clicked,
                          this,
                          [=, this]() { SelectProduct(band); });
      }
   }
   ~SatelliteProductsWidgetImpl() = default;

   void NormalizeProductButtons();
   void SelectProduct(common::SatelliteBand band);
   void UpdateProductSelection(common::SatelliteBand band);

   SatelliteProductsWidget* self_;
   QLayout*                 layout_;
   std::list<QToolButton*>  productButtons_;
};

SatelliteProductsWidget::SatelliteProductsWidget(QWidget* parent) :
    QWidget(parent), p {std::make_shared<SatelliteProductsWidgetImpl>(this)}
{
}

SatelliteProductsWidget::~SatelliteProductsWidget() = default;

void SatelliteProductsWidget::showEvent(QShowEvent* event)
{
   QWidget::showEvent(event);

   p->NormalizeProductButtons();
}

void SatelliteProductsWidgetImpl::NormalizeProductButtons()
{
   int satelliteMaxWidth = 0;

   // Set each satellite product's tool button to the same size
   std::for_each(productButtons_.cbegin(),
                 productButtons_.cend(),
                 [&](auto& toolButton)
                 {
                    if (toolButton->isVisible())
                    {
                       satelliteMaxWidth =
                          std::max(satelliteMaxWidth, toolButton->width());
                    }
                 });

   if (satelliteMaxWidth > 0)
   {
      std::for_each(productButtons_.cbegin(),
                    productButtons_.cend(),
                    [&](auto& toolButton)
                    { toolButton->setMinimumWidth(satelliteMaxWidth); });
   }
}

void SatelliteProductsWidgetImpl::SelectProduct(common::SatelliteBand band)
{
   UpdateProductSelection(band);

   Q_EMIT self_->RadarProductSelected(common::RadarProductGroup::Satellite,
                                      common::GetSatelliteBandName(band),
                                      0);
}

void SatelliteProductsWidget::UpdateProductSelection(
   common::RadarProductGroup group, const std::string& productName)
{
   if (group == common::RadarProductGroup::Satellite)
   {
      common::SatelliteBand band = common::GetSatelliteBand(productName);
      p->UpdateProductSelection(band);
   }
   else
   {
      p->UpdateProductSelection(common::SatelliteBand::Unknown);
   }
}

void SatelliteProductsWidgetImpl::UpdateProductSelection(
   common::SatelliteBand band)
{
   const std::string& productName = common::GetSatelliteBandName(band);

   std::for_each(productButtons_.cbegin(),
                 productButtons_.cend(),
                 [&](auto& toolButton)
                 {
                    if (toolButton->text().toStdString() == productName)
                    {
                       toolButton->setCheckable(true);
                       toolButton->setChecked(true);
                    }
                    else
                    {
                       toolButton->setChecked(false);
                       toolButton->setCheckable(false);
                    }
                 });
}

} // namespace ui
} // namespace qt
} // namespace scwx

#include "satellite_products_widget.moc"
