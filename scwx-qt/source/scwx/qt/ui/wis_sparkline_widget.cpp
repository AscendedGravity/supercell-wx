#include <scwx/qt/ui/wis_sparkline_widget.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QFontMetrics>
#include <QPalette>

namespace scwx::qt::ui
{

static const QColor kBackgroundColor =
   QColor(0.08 * 255, 0.08 * 255, 0.15 * 255);

static QColor ScoreColor(double value, double threshold)
{
   if (threshold <= 0.0)
   {
      return QColor(0x55, 0xff, 0x55); // green
   }
   if (value >= threshold)
   {
      return QColor(0xff, 0x55, 0x55); // red
   }
   if (value >= threshold * 0.75)
   {
      return QColor(0xff, 0xaa, 0x00); // orange
   }
   if (value >= threshold * 0.5)
   {
      return QColor(0xff, 0xff, 0x55); // yellow
   }
   return QColor(0x55, 0xff, 0x55); // green
}

class WisSparklineWidget::Impl
{
public:
   explicit Impl(WisSparklineWidget* self) : self_(self) {}
   ~Impl() = default;

   void UpdatePens()
   {
      const QPalette& pal = self_->palette();
      textColor_          = pal.color(QPalette::WindowText);
   }

   WisSparklineWidget* self_;
   std::vector<double> data_ {};
   double              threshold_ {0.0};
   QColor              textColor_ {Qt::white};
};

WisSparklineWidget::WisSparklineWidget(QWidget* parent) :
    QWidget(parent), p(std::make_unique<Impl>(this))
{
   setMinimumSize(200, 80);
   setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

WisSparklineWidget::~WisSparklineWidget() = default;

void WisSparklineWidget::SetData(const std::vector<double>& values)
{
   p->data_ = values;
   update();
}

void WisSparklineWidget::SetThreshold(double threshold)
{
   p->threshold_ = threshold;
   update();
}

QSize WisSparklineWidget::minimumSizeHint() const
{
   return QSize(200, 80);
}

QSize WisSparklineWidget::sizeHint() const
{
   return QSize(400, 120);
}

void WisSparklineWidget::paintEvent(QPaintEvent* /*event*/)
{
   QPainter painter(this);
   painter.setRenderHint(QPainter::Antialiasing, true);

   // Background
   painter.fillRect(rect(), kBackgroundColor);

   if (p->data_.empty())
   {
      painter.setPen(p->textColor_);
      painter.drawText(rect(), Qt::AlignCenter, tr("No score history"));
      return;
   }

   p->UpdatePens();

   const double w = static_cast<double>(width());
   const double h = static_cast<double>(height());

   // Margins
   constexpr double kMargin    = 8.0;
   constexpr double kTopMargin = 18.0;
   const double     plotLeft   = kMargin;
   const double     plotRight  = w - kMargin;
   const double     plotTop    = kTopMargin;
   const double     plotBottom = h - kMargin;

   if (plotRight <= plotLeft || plotBottom <= plotTop)
   {
      return;
   }

   // Find min/max values
   double minVal = std::numeric_limits<double>::max();
   double maxVal = std::numeric_limits<double>::lowest();
   for (const auto& value : p->data_)
   {
      minVal = std::min(minVal, value);
      maxVal = std::max(maxVal, value);
   }

   // Add padding to the range
   const double range = maxVal - minVal;
   if (range < 0.001)
   {
      minVal -= 1.0;
      maxVal += 1.0;
   }
   else
   {
      const double pad = range * 0.1;
      minVal -= pad;
      maxVal += pad;
   }

   const double plotW = plotRight - plotLeft;
   const double plotH = plotBottom - plotTop;

   auto MapX = [&](size_t index) -> double
   {
      const size_t n = p->data_.size();
      if (n <= 1)
      {
         return plotLeft + plotW * 0.5;
      }
      const double t = static_cast<double>(index) / static_cast<double>(n - 1);
      return plotLeft + t * plotW;
   };

   auto MapY = [&](double value) -> double
   {
      const double t = (value - minVal) / (maxVal - minVal);
      return plotBottom - t * plotH;
   };

   // Build the line path
   QPainterPath linePath;
   bool         first = true;
   for (size_t i = 0; i < p->data_.size(); ++i)
   {
      const double x = MapX(i);
      const double y = MapY(p->data_[i]);
      if (first)
      {
         linePath.moveTo(x, y);
         first = false;
      }
      else
      {
         linePath.lineTo(x, y);
      }
   }

   // Color the line based on the last value
   const double lastVal   = p->data_.empty() ? 0.0 : p->data_.back();
   const QColor lineColor = ScoreColor(lastVal, p->threshold_);

   // Draw the line
   QPen linePen(lineColor, 2.0);
   painter.setPen(linePen);
   painter.setBrush(Qt::NoBrush);
   painter.drawPath(linePath);

   // Draw threshold line (dashed)
   if (p->threshold_ > 0.0 && minVal <= p->threshold_ &&
       maxVal >= p->threshold_)
   {
      const double ty = MapY(p->threshold_);
      QPen         threshPen(QColor(0xff, 0x55, 0x55), 1.0, Qt::DashLine);
      painter.setPen(threshPen);
      painter.drawLine(QPointF(plotLeft, ty), QPointF(plotRight, ty));

      // Threshold label
      painter.setPen(QColor(0xff, 0x55, 0x55));
      QFont threshFont = painter.font();
      threshFont.setPointSize(threshFont.pointSize() - 2);
      painter.setFont(threshFont);
      painter.drawText(QPointF(plotLeft + 2, ty - 2),
                       QString("Threshold: %1").arg(p->threshold_, 0, 'f', 1));
   }

   // Draw min/max labels
   QFont labelFont = painter.font();
   labelFont.setPointSize(labelFont.pointSize() - 2);
   painter.setFont(labelFont);
   painter.setPen(p->textColor_);

   // Max label at top
   auto         maxIt    = std::max_element(p->data_.begin(), p->data_.end());
   size_t       maxIdx   = std::distance(p->data_.begin(), maxIt);
   double       maxValue = *maxIt;
   const double maxX     = MapX(maxIdx);
   const double maxY     = MapY(maxValue);
   QRectF       maxLabelRect(maxX - 30, maxY - 16, 60, 14);
   painter.drawText(
      maxLabelRect, Qt::AlignCenter, QString::number(maxValue, 'f', 1));

   // Min label at bottom
   auto         minIt     = std::min_element(p->data_.begin(), p->data_.end());
   size_t       minIdx    = std::distance(p->data_.begin(), minIt);
   double       minValue2 = *minIt;
   const double minX      = MapX(minIdx);
   const double minY      = MapY(minValue2);
   QRectF       minLabelRect(minX - 30, minY + 2, 60, 14);
   painter.drawText(
      minLabelRect, Qt::AlignCenter, QString::number(minValue2, 'f', 1));
}

} // namespace scwx::qt::ui
