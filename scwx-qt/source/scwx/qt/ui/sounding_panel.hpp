#pragma once

#include <memory>

#include <QDockWidget>
#include <QString>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QScrollArea;

namespace scwx::qt::ui
{

class SoundingPanelImpl;

/**
 * @brief Floating dock widget for displaying rustwx sounding PNGs.
 *
 * Provides controls for selecting a location (lat/lon), model, cycle,
 * and forecast hour. Fetches a sounding PNG via the rustwx sounding_plot
 * binary and displays the result in a scrollable image viewer.
 */
class SoundingPanel : public QDockWidget
{
   Q_OBJECT

public:
   explicit SoundingPanel(QWidget* parent = nullptr);
   ~SoundingPanel();

   SoundingPanel(const SoundingPanel&)            = delete;
   SoundingPanel& operator=(const SoundingPanel&) = delete;

   void SetLocation(double lat, double lon);
   void RequestSounding();

public slots:
   void OnSoundingImageReady(const QString& imagePath);
   void OnSoundingError(const QString& message);
   void OnFetchClicked();

signals:
   /**
    * @brief Emitted when the user clicks "Select Point" to pick a location
    *        from the map.
    */
   void PointSelectionStarted();

private:
   std::unique_ptr<SoundingPanelImpl> p;
};

} // namespace scwx::qt::ui
