#pragma once

#include <memory>

#include <QDialog>
#include <QString>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;

namespace scwx::qt::ui
{

class SoundingDialogImpl;

/**
 * @brief Floating dialog for generating and displaying rustwx sounding PNGs.
 *
 * Pre-populated with lat/lon from a map right-click. User can adjust
 * parameters (model, cycle, forecast hour) and click "Fetch Sounding".
 * The resulting SHARPpy-style PNG is displayed in a scrollable area.
 */
class SoundingDialog : public QDialog
{
   Q_OBJECT

public:
   explicit SoundingDialog(QWidget* parent = nullptr);
   ~SoundingDialog();

   SoundingDialog(const SoundingDialog&)            = delete;
   SoundingDialog& operator=(const SoundingDialog&) = delete;

   void SetLocation(double lat, double lon);

public slots:
   void OnSoundingImageReady(const QString& imagePath);
   void OnSoundingError(const QString& message);
   void OnFetchClicked();

private:
   std::unique_ptr<SoundingDialogImpl> p;
};

} // namespace scwx::qt::ui
