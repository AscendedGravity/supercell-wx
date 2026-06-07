#pragma once

#include <QDialog>
#include <memory>

namespace Ui
{
class WisDetailsDialog;
}

namespace scwx::qt::ui
{

class WisDetailsDialog : public QDialog
{
   Q_OBJECT

public:
   explicit WisDetailsDialog(QWidget* parent = nullptr);
   ~WisDetailsDialog();

   void UpdateData();

private:
   class Impl;
   std::unique_ptr<Impl> p;
   Ui::WisDetailsDialog* ui;
};

} // namespace scwx::qt::ui
