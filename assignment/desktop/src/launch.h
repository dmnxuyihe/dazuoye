#pragma once
#include "ui.h"
#include <QCommandLineParser>
template <class Window>
int launch(int argc, char **argv, const QString &role, const QStringList &pages) {
    // 管理端和用户端共用的启动模板：Window 模板参数决定最终创建哪一种主窗口。
    QApplication app(argc, argv);
    app.setOrganizationName("Assignment");
    app.setApplicationName("Electra-" + role);
    applyTheme(app);
    // 命令行参数便于切换 API、缓存位置，也为自动截图验收提供入口。
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"api", "统一 FastAPI 地址", "url", "http://127.0.0.1:4173"});
    parser.addOption({"cache", "SQLite 缓存文件", "path"});
    parser.addOption({"screenshots", "自动保存原生窗口截图后退出", "directory"});
    parser.addOption({"page", "启动页面", "name"});
    parser.addOption({"snapshot-delay", "截图等待毫秒", "ms", "1800"});
    parser.process(app);
    // 只接受无内嵌账号密码的 HTTP(S) 地址，避免错误协议和凭据泄漏。
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
    // 对象按 cache -> api -> window 的顺序构造，退出时反序析构，生命周期安全。
    CacheStore cache(path);
    ApiClient api(base, &cache);
    Window window(&api, &cache);
    window.show();
    // singleShot 延迟导航，确保窗口布局与首轮事件循环已经建立。
    if (!parser.value("page").isEmpty())
        QTimer::singleShot(2000, &window, [&] { window.navigate(parser.value("page")); });
    if (parser.isSet("screenshots"))
        window.screenshot(parser.value("screenshots"), pages,
                          parser.value("snapshot-delay").toInt());
    // 进入 Qt 事件循环，之后按钮、网络、定时器等信号槽才会持续工作。
    return app.exec();
}
