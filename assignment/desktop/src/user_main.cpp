#include "launch.h"
#include "user_window.h"
int main(int argc, char **argv) {
    // 用户端进程入口：业务初始化、参数解析和事件循环均由通用 launch 模板完成。
    return launch<UserWindow>(argc, argv, "User",
                              {"home", "map", "station", "charging", "profile"});
}
