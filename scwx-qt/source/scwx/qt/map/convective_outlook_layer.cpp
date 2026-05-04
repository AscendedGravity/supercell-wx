#include <scwx/qt/map/convective_outlook_layer.hpp>
#include <scwx/spc/spc_types.hpp>
#include <scwx/qt/manager/spc_outlook_manager.hpp>
#include <scwx/util/logger.hpp>

#include <QMapLibre/Map>

#include <QByteArray>
#include <QJsonDocument>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <string>

namespace scwx::qt::map
{

static const std::string logPrefix_ = "scwx::qt::map::convective_outlook_layer";
static const auto        logger_    = scwx::util::Logger::Create(logPrefix_);

static const std::string kSourceId_    = "convective-outlook-source";
static const std::string kFillLayerId_ = "convective-outlook-fill";
static const std::string kLineLayerId_ = "convective-outlook-line";

class ConvectiveOutlookLayer::Impl
{
public:
   Impl()  = default;
   ~Impl() = default;

   std::string before_ {};

   QVariantMap BuildFeatureCollection()
   {
      auto& manager = manager::SpcOutlookManager::Instance();
      auto  data    = manager.GetOutlookData();

      QVariantMap collection;
      collection["type"] = "FeatureCollection";

      QVariantList features;

      if (data == nullptr)
      {
         collection["features"] = features;
         return collection;
      }

      for (const auto& polygon : data->polygons_)
      {
         if (polygon.rings_.empty())
         {
            continue;
         }

         QVariantMap feature;
         feature["type"] = "Feature";

         QVariantMap geometry;
         geometry["type"] = "Polygon";

         QVariantList coords;
         for (const auto& ring : polygon.rings_)
         {
            QVariantList ringCoords;
            for (const auto& pt : ring)
            {
               QVariantList coord;
               coord << pt.second << pt.first;
               ringCoords << QVariant(coord);
            }
            coords << QVariant(ringCoords);
         }
         geometry["coordinates"] = coords;
         feature["geometry"]     = geometry;

         QVariantMap props;
         props["dn"]           = polygon.dn_;
         feature["properties"] = props;

         features << QVariant(feature);
      }

      collection["features"] = features;
      return collection;
   }
};

ConvectiveOutlookLayer::ConvectiveOutlookLayer() : p(std::make_unique<Impl>())
{
}

ConvectiveOutlookLayer::~ConvectiveOutlookLayer() = default;

const std::string& ConvectiveOutlookLayer::sourceId()
{
   return kSourceId_;
}

const std::string& ConvectiveOutlookLayer::fillLayerId()
{
   return kFillLayerId_;
}

const std::string& ConvectiveOutlookLayer::lineLayerId()
{
   return kLineLayerId_;
}

void ConvectiveOutlookLayer::Add(std::shared_ptr<QMapLibre::Map> map,
                                 const std::string&              before)
{
   logger_->debug("Add()");

   p->before_ = before;

   Remove(map);

   QVariantMap fc = p->BuildFeatureCollection();

   auto featuresList = fc["features"].toList();
   if (featuresList.isEmpty())
   {
      logger_->debug("No features to add");
      return;
   }

   QString beforeStr = QString::fromStdString(before);

   // Add GeoJSON source with FeatureCollection (JSON string)
   QVariantMap sourceOpts;
   sourceOpts["type"] = "geojson";
   sourceOpts["data"] =
      QByteArray(QJsonDocument::fromVariant(fc).toJson(QJsonDocument::Compact));
   map->addSource(QString::fromStdString(kSourceId_), sourceOpts);

   auto& manager = manager::SpcOutlookManager::Instance();
   int   opacity = manager.GetOpacity();

   // Data-driven fill color based on DN property
   // Note: wrap inner QVariantList in QVariant() to prevent flattening by
   // QList::operator<<
   QVariantList fillColorExpr;
   fillColorExpr << "match" << QVariant(QVariantList {} << "get" << "dn") << 2
                 << "#C1E9C1" << 3 << "#008B00" << 4 << "#FFFF00" << 5
                 << "#FFA500" << 6 << "#FF0000" << 8 << "#FF00FF"
                 << "#888888";

   // Serialize expression to JSON string for proper parsing by MapLibre
   QByteArray fillColorJson =
      QJsonDocument::fromVariant(QVariant(fillColorExpr))
         .toJson(QJsonDocument::Compact);

   // Add fill layer
   map->addLayer(
      QString::fromStdString(kFillLayerId_),
      {{"type", "fill"}, {"source", QString::fromStdString(kSourceId_)}},
      beforeStr);
   map->setPaintProperty(QString::fromStdString(kFillLayerId_),
                         "fill-color",
                         QString::fromUtf8(fillColorJson));
   map->setPaintProperty(
      QString::fromStdString(kFillLayerId_), "fill-opacity", opacity / 100.0);

   // Add line layer for borders
   map->addLayer(
      QString::fromStdString(kLineLayerId_),
      {{"type", "line"}, {"source", QString::fromStdString(kSourceId_)}},
      beforeStr);
   map->setPaintProperty(QString::fromStdString(kLineLayerId_),
                         "line-color",
                         QString::fromUtf8(fillColorJson));
   map->setPaintProperty(
      QString::fromStdString(kLineLayerId_), "line-width", 1.5f);
   map->setPaintProperty(
      QString::fromStdString(kLineLayerId_), "line-opacity", opacity / 100.0);
}

void ConvectiveOutlookLayer::Remove(std::shared_ptr<QMapLibre::Map> map)
{
   QString sourceId = QString::fromStdString(kSourceId_);
   QString fillId   = QString::fromStdString(kFillLayerId_);
   QString lineId   = QString::fromStdString(kLineLayerId_);

   if (map->layerExists(lineId))
   {
      map->removeLayer(lineId);
   }
   if (map->layerExists(fillId))
   {
      map->removeLayer(fillId);
   }
   if (map->sourceExists(sourceId))
   {
      map->removeSource(sourceId);
   }
}

void ConvectiveOutlookLayer::Update(std::shared_ptr<QMapLibre::Map> map)
{
   if (!map->sourceExists(QString::fromStdString(kSourceId_)))
   {
      Add(map, p->before_);
      return;
   }

   QVariantMap fc = p->BuildFeatureCollection();

   auto featuresList = fc["features"].toList();
   if (featuresList.isEmpty())
   {
      Remove(map);
      return;
   }

   auto& manager = manager::SpcOutlookManager::Instance();
   int   opacity = manager.GetOpacity();

   // Update source data (JSON string)
   QVariantMap update;
   update["data"] =
      QByteArray(QJsonDocument::fromVariant(fc).toJson(QJsonDocument::Compact));
   map->updateSource(QString::fromStdString(kSourceId_), update);

   // Update opacity
   map->setPaintProperty(
      QString::fromStdString(kFillLayerId_), "fill-opacity", opacity / 100.0);
   map->setPaintProperty(
      QString::fromStdString(kLineLayerId_), "line-opacity", opacity / 100.0);
}

} // namespace scwx::qt::map
