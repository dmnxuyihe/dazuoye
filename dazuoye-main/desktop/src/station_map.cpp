#include "station_map.h"
#include "client.h"
#include <QWebChannel>
#include <QUrlQuery>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestInterceptor>
#include <cmath>
namespace {
class MapRequests final : public QWebEngineUrlRequestInterceptor {
  public:
    using QWebEngineUrlRequestInterceptor::QWebEngineUrlRequestInterceptor;
    void interceptRequest(QWebEngineUrlRequestInfo &info) override {
        const auto url = info.requestUrl();
        if (url.scheme() == "qrc" || url.scheme() == "data" || url.scheme() == "about")
            return;
        if (url.scheme() == "https" && url.host() == "tile.openstreetmap.org") {
            return;
        }
        info.block(true);
    }
};
QWebEngineProfile *mapProfile() {
    static auto profile = [] {
        auto p = new QWebEngineProfile("electra-map", qApp);
        p->setHttpUserAgent(p->httpUserAgent() + " ElectraCharging/1.0");
        p->setHttpCacheType(QWebEngineProfile::DiskHttpCache);
        p->setHttpCacheMaximumSize(128 * 1024 * 1024);
        p->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);
        p->setUrlRequestInterceptor(new MapRequests(p));
        return p;
    }();
    return profile;
}
}
StationMap::StationMap(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(180);
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    view = new QWebEngineView(mapProfile(), this);
    view->setObjectName("online-station-map");
    view->setContextMenuPolicy(Qt::NoContextMenu);
    view->page()->setBackgroundColor(QColor("#181321"));
    view->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
    view->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, false);
    view->settings()->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, false);
    connect(view->page(), &QWebEnginePage::featurePermissionRequested, this,
        [this](const QUrl &origin, QWebEnginePage::Feature feature) {
            view->page()->setFeaturePermission(origin, feature,
                feature == QWebEnginePage::Geolocation ? QWebEnginePage::PermissionGrantedByUser
                    : QWebEnginePage::PermissionDeniedByUser);
        });
    auto channel = new QWebChannel(view);
    bridge = new StationMapBridge(channel);
    connect(bridge, &StationMapBridge::picked, this, &StationMap::selectStation);
    connect(bridge, &StationMapBridge::located, this, &StationMap::setLocation);
    connect(bridge, &StationMapBridge::addressRequested, this, [this](const QString &address) {
        if (!api || address.trimmed().size()<2) { message("请输入城市和详细地址"); return; }
        message("正在解析地址…");
        api->get("/public/map/geocode?address="+QString::fromLatin1(QUrl::toPercentEncoding(address.trimmed())),this,[this](const Reply &r) {
            if (!r.ok) { message(r.error+"；可选择城市或地图选位置"); return; }
            view->page()->runJavaScript("window.showAddresses("+QString::fromUtf8(r.data.toJson(QJsonDocument::Compact))+")");
        });
    });
    connect(bridge, &StationMapBridge::navigationRequested, this, [this](const QString &id) {
        if (!api || !hasLocation) { message("请先定位、解析地址或地图选位置"); return; }
        for (auto v : stations) {
            const auto o=v.toObject(); if(o["id"].toString()!=id) continue;
            const int generation=++routeGeneration;
            message("正在规划驾车路线…");
            const auto path=QString("/public/map/route?latitude=%1&longitude=%2&to_latitude=%3&to_longitude=%4")
                .arg(myLatitude,0,'f',6).arg(myLongitude,0,'f',6)
                .arg(o["latitude"].toDouble(),0,'f',6).arg(o["longitude"].toDouble(),0,'f',6);
            api->get(path,this,[this,generation](const Reply &r) {
                if(generation!=routeGeneration)return;
                if(!r.ok){message(r.error);return;}
                view->page()->runJavaScript("window.showRoute("+QString::fromUtf8(r.data.toJson(QJsonDocument::Compact))+")");
            });
        }
    });
    channel->registerObject("stationBridge", bridge);
    view->page()->setWebChannel(channel);
    layout->addWidget(view);
    connect(view, &QWebEngineView::loadFinished, this, [this](bool ok) {
        ready = ok;
        if (ok) { setCompact(compact); sync(); if(hasLocation) view->page()->runJavaScript(QString("updateLocation(%1,%2,false)").arg(myLatitude,0,'f',6).arg(myLongitude,0,'f',6)); }
    });
    auto read = [](const QString &path) {
        QFile file(path); file.open(QIODevice::ReadOnly); return QString::fromUtf8(file.readAll());
    };
    auto html = read(":/map/map.html");
    html.replace("<link rel=\"stylesheet\" href=\"leaflet.css\">", "<style>" + read(":/map/leaflet.css") + "</style>");
    html.replace("<script src=\"leaflet.js\"></script>", "<script>" + read(":/map/leaflet.js") + "</script>");
    html.replace("<script src=\"qrc:///qtwebchannel/qwebchannel.js\"></script>", "<script>" + read(":/qtwebchannel/qwebchannel.js") + "</script>");
    QFile font(":/map/map-labels.woff2"); font.open(QIODevice::ReadOnly);
    html.replace("</head>", "<style>@font-face{font-family:MapLabels;src:url(data:font/woff2;base64," + QString::fromLatin1(font.readAll().toBase64()) + ")}html,body,button,.leaflet-container,.leaflet-tooltip,.leaflet-control{font-family:MapLabels,sans-serif}</style></head>");
    view->setHtml(html, QUrl("https://lv-l40s-liuzihang.taild6df1c.ts.net/qt/"));
}
void StationMap::setCompact(bool enabled) {
    compact = enabled;
    if (ready) view->page()->runJavaScript(QString("document.body.classList.toggle('compact',%1)").arg(enabled ? "true" : "false"));
}
void StationMap::setStations(const QJsonArray &rows, const QString &id) {
    stations = {};
    selected = id;
    for (const auto &v : rows) {
        const auto o = v.toObject();
        bool lonOk = false, latOk = false;
        double lon = o["longitude"].toVariant().toDouble(&lonOk);
        double lat = o["latitude"].toVariant().toDouble(&latOk);
        if (!lonOk || !latOk || !std::isfinite(lon) || !std::isfinite(lat) ||
            lon < -180 || lon > 180 || lat < -85 || lat > 85) continue;
        stations.append(QJsonObject{{"id", o["id"]}, {"name", o["name"]},
                                    {"longitude", lon}, {"latitude", lat}});
    }
    sync();
}
void StationMap::sync() {
    if (!ready) return;
    const auto payload = QJsonDocument(QJsonObject{{"rows", stations}, {"selected", selected}})
                             .toJson(QJsonDocument::Compact);
    view->page()->runJavaScript("window.setStations(" + QString::fromUtf8(payload) + ")");
}
void StationMap::selectStation(const QString &id) {
    for (const auto &v : stations)
        if (v.toObject()["id"].toString() == id) {
            selected = id;
            emit stationSelected(id);
            return;
        }
}
void StationMap::fitStations(bool allCities) {
    if (ready) view->page()->runJavaScript(allCities ? "window.fitStations(true)" : "window.fitStations(false)");
}
void StationMap::zoomAt(double factor, const QPointF &anchor) {
    if (ready && factor > 0)
        view->page()->runJavaScript(QString("map.setZoomAround([%1,%2],map.getZoom()+%3)")
            .arg(anchor.x()).arg(anchor.y()).arg(std::log2(factor)));
}

void StationMap::message(const QString &text) {
    auto json=QString::fromUtf8(QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact));
    view->page()->runJavaScript("window.mapMessage("+json+"[0])");
}
void StationMap::setLocation(double lat, double lon) {
    if(!std::isfinite(lat)||!std::isfinite(lon)||qAbs(lat)>85||qAbs(lon)>180)return;
    myLatitude=lat; myLongitude=lon; hasLocation=true; ++routeGeneration;
    if(api) {
        QSettings settings;
        settings.setValue("map/"+api->baseUrl().toString()+"/lat",lat);
        settings.setValue("map/"+api->baseUrl().toString()+"/lon",lon);
    }
    if(ready)view->page()->runJavaScript(QString("updateLocation(%1,%2,false)").arg(lat,0,'f',6).arg(lon,0,'f',6));
    emit locationChanged(lat,lon);
}
void StationMap::setApi(ApiClient *client) {
    if(api)return;
    api=client;
    QTimer::singleShot(0,this,[this] {
        QSettings settings;const auto prefix="map/"+api->baseUrl().toString();
        if(settings.contains(prefix+"/lat"))setLocation(settings.value(prefix+"/lat").toDouble(),settings.value(prefix+"/lon").toDouble());
    });
    auto poll=new QTimer(this);
    connect(poll,&QTimer::timeout,this,[this] {
        if(!isVisible())return;
        api->request("GET","/public/map/preview-location",{},this,[this](const Reply &r) {
            const auto o=r.data.object();const auto stamp=o["updated_at"].toDouble();
            if(r.ok&&stamp>previewStamp) {previewStamp=stamp;setLocation(o["latitude"].toDouble(),o["longitude"].toDouble());}
        },false);
    });
    poll->start(3000);
}
