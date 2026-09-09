#pragma once
#include "ui.h"
#include <QCommandLineParser>
template <class Window>
int launch(int argc, char **argv, const QString &role, const QStringList &pages) {
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);
    app.setOrganizationName("Assignment");
    app.setApplicationName("Electra-" + role);
    applyTheme(app);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"api", "统一 FastAPI 地址", "url", "http://127.0.0.1:4173"});
    parser.addOption({"cache", "SQLite 缓存文件", "path"});
    parser.addOption({"screenshots", "自动保存原生窗口截图后退出", "directory"});
    parser.addOption({"page", "启动页面", "name"});
    parser.addOption({"snapshot-delay", "截图等待毫秒", "ms", "1800"});
    parser.process(app);
    QUrl base(parser.value("api"));
    if (!base.isValid() || (base.scheme() != "http" && base.scheme() != "https") ||
        base.host().isEmpty() || !base.userInfo().isEmpty()) {
        qCritical("Invalid API URL");
        return 2;
    }
    auto path = parser.value("cache");
    if (path.isEmpty())
        path = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
               "/cache.sqlite";
    CacheStore cache(path);
    ApiClient api(base, &cache);
    Window window(&api, &cache);
    window.show();
    if (!parser.value("page").isEmpty())
        QTimer::singleShot(2000, &window, [&] { window.navigate(parser.value("page")); });
    if (parser.isSet("screenshots"))
        window.screenshot(parser.value("screenshots"), pages,
                          parser.value("snapshot-delay").toInt());
    return app.exec();
}
