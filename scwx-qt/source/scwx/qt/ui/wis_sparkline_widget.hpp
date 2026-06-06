#pragma once

#include <memory>
#include <vector>
#include <utility>

#include <QSize>
#include <QWidget>

namespace scwx::qt::ui
{

class WisSparklineWidget : public QWidget
{
   Q_OBJECT

public:
   explicit WisSparklineWidget(QWidget* parent = nullptr);
   ~WisSparklineWidget();

   void SetData(const std::vector<std::pair<int, double>>& values);
   void SetThreshold(double threshold);

   QSize minimumSizeHint() const override;
   QSize sizeHint() const override;

protected:
   void paintEvent(QPaintEvent* event) override;

private:
   class Impl;
   std::unique_ptr<Impl> p;
};

} // namespace scwx::qt::ui
