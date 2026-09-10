#include "station_map.h"
#include <QSignalSpy>
#include <QtTest>
class MapTest : public QObject {
    Q_OBJECT
    QVariant js(QWebEngineView *view, const QString &source) {
        QVariant result; QEventLoop loop;
        view->page()->runJavaScript(source, [&](const QVariant &v) {result=v;loop.quit();});
        QTimer::singleShot(5000,&loop,&QEventLoop::quit);loop.exec();return result;
    }
  private slots:
    void onlineMapAndSelection() {
        StationMap map;map.resize(820,600);map.show();
        const QJsonArray rows{
            QJsonObject{{"id","a"},{"name","前海站"},{"longitude",113.9},{"latitude",22.53}},
            QJsonObject{{"id","b"},{"name","上海站"},{"longitude",121.47},{"latitude",31.23}},
            QJsonObject{{"id","invalid"},{"name","缺失坐标"}}};
        map.setStations(rows,"a");
        auto view=map.findChild<QWebEngineView*>();QVERIFY(view);
        QTRY_COMPARE_WITH_TIMEOUT(js(view,"typeof markers!=='undefined'?markers.size:0").toInt(),2,15000);
        QTRY_VERIFY_WITH_TIMEOUT(js(view,
            "Array.from(document.fonts).some(f => f.family === 'MapLabels' && f.status === 'loaded')").toBool(),5000);
        for (const auto selector : {"#map", ".map-tools-toggle", ".tools select", ".tools input", ".leaflet-tooltip", "#status"}) {
            QVERIFY2(js(view, QString("getComputedStyle(document.querySelector('%1')).fontFamily.includes('MapLabels')")
                .arg(selector)).toBool(), selector);
        }
        // Dynamic station/address characters absent from the former fixture subset.
        QVERIFY(js(view, R"JS((() => {
            const canvas = document.createElement('canvas');
            canvas.width = 64; canvas.height = 64;
            const ctx = canvas.getContext('2d');
            ctx.font = '32px MapLabels';
            const pixels = text => {
                ctx.clearRect(0, 0, 64, 64); ctx.fillText(text, 2, 40);
                return canvas.toDataURL();
            };
            const missing = pixels(String.fromCodePoint(0x10ffff));
            return ['科', '技'].every(text => pixels(text) !== missing);
        })())JS").toBool());
        QVERIFY(js(view,"map.getCenter().lng < 115").toBool());
        QSignalSpy spy(&map,&StationMap::stationSelected);
        QTRY_VERIFY_WITH_TIMEOUT(js(view,"!!bridge").toBool(),5000);
        js(view,"markers.get('b').fire('click');true");
        QTRY_COMPARE(spy.size(),1);QCOMPARE(spy.first().first().toString(),QString("b"));
        map.selectStation("invalid");QCOMPARE(spy.size(),1);
        map.setStations(rows,"b");
        map.fitStations(false);
        QTRY_VERIFY(js(view,"map.getCenter().lng > 120").toBool());
        auto before=js(view,"map.getZoom()").toDouble();
        map.zoomAt(2,QPointF(410,300));
        QTRY_VERIFY(js(view,"map.getZoom()").toDouble()>before);
        map.fitStations(true);
        QTRY_VERIFY(js(view,"map.getBounds().contains([22.53,113.9]) && map.getBounds().contains([31.23,121.47])").toBool());
        // Explicitly enabled online check; deterministic interaction test also works offline.
        if(qEnvironmentVariableIsSet("ELECTRA_TEST_ONLINE_MAP"))
            QTRY_VERIFY_WITH_TIMEOUT(js(view,"window.loadedTiles||0").toInt()>0,45000);
        map.fitStations(false);
        if(qEnvironmentVariableIsSet("ELECTRA_TEST_ONLINE_MAP")) {
            QTRY_VERIFY_WITH_TIMEOUT(js(view,"document.querySelectorAll('.leaflet-tile-loaded').length").toInt()>0,45000);
            QTest::qWait(1200);
        }
        const auto path=qEnvironmentVariable("ELECTRA_MAP_SCREENSHOT");
        if(!path.isEmpty())map.grab().save(path);
    }
};
QTEST_MAIN(MapTest)
#include "test_map.moc"
