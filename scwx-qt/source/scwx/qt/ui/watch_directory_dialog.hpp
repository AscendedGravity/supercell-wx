#pragma once

#include <QDialog>
#include <memory>

namespace Ui
{
class WatchDirectoryDialog;
} // namespace Ui

namespace scwx
{
namespace qt
{
namespace ui
{

class WatchDirectoryDialogImpl;

class WatchDirectoryDialog : public QDialog
{
   Q_OBJECT
   Q_DISABLE_COPY(WatchDirectoryDialog)

public:
   explicit WatchDirectoryDialog(QWidget* parent = nullptr);
   ~WatchDirectoryDialog();

private:
   friend class WatchDirectoryDialogImpl;
   std::unique_ptr<WatchDirectoryDialogImpl> p;
   Ui::WatchDirectoryDialog*                 ui;
};

} // namespace ui
} // namespace qt
} // namespace scwx
