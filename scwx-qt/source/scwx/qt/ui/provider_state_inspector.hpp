#pragma once

#include <QDialog>
#include <memory>

namespace Ui
{
class ProviderStateInspector;
} // namespace Ui

namespace scwx
{
namespace qt
{
namespace ui
{

class ProviderStateInspectorImpl;

class ProviderStateInspector : public QDialog
{
   Q_OBJECT
   Q_DISABLE_COPY(ProviderStateInspector)

public:
   explicit ProviderStateInspector(QWidget* parent = nullptr);
   ~ProviderStateInspector();

private:
   friend class ProviderStateInspectorImpl;
   std::unique_ptr<ProviderStateInspectorImpl> p;
   Ui::ProviderStateInspector*                 ui;
};

} // namespace ui
} // namespace qt
} // namespace scwx
