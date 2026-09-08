#include "admin_window.h"
#include "launch.h"
int main(int argc, char **argv) {
    // 管理端进程入口：把窗口类型和自动截图可遍历的页面名交给通用 launch 模板。
    return launch<AdminWindow>(argc, argv, "Admin",
                               {"dashboard", "station", "trips", "history", "forecast"});
}
