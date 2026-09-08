cmake_minimum_required(VERSION 3.21)
# 定义原生 Qt 桌面工程；C++17 是源码使用的最低语言标准。
project(ElectraDesktop VERSION 1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
## 自动运行 Qt 元对象编译器和资源编译器，使 Q_OBJECT/信号槽及 qrc 资源生效。
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
# Widgets 构建界面，Network/WebSockets 连接服务，Sql 缓存，Svg/Charts 负责视觉组件。
find_package(Qt6 6.5.3 REQUIRED COMPONENTS Widgets Network WebSockets Sql Svg Charts Test)
if(MSVC)
  add_compile_options(/utf-8 /W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()
# 两个可执行程序共享网络层和 UI 基础设施，静态库避免重复维护源码列表。
add_library(electra_common STATIC src/client.cpp src/client.h src/ui.cpp src/ui.h resources.qrc)
target_include_directories(electra_common PUBLIC src)
target_link_libraries(electra_common PUBLIC Qt6::Widgets Qt6::Network Qt6::WebSockets Qt6::Sql Qt6::Svg)
# 用户端与管理员端分别生成独立 EXE；只有管理员端需要 Qt Charts。
qt_add_executable(electra-user src/user_main.cpp src/user_window.cpp src/user_window.h)
target_link_libraries(electra-user PRIVATE electra_common)
qt_add_executable(electra-admin src/admin_main.cpp src/admin_window.cpp src/admin_window.h)
target_link_libraries(electra-admin PRIVATE electra_common Qt6::Charts)
# BUILD_TESTING=ON 时才编译 Qt Test 测试，发布包可关闭以减少构建时间。
include(CTest)
if(BUILD_TESTING)
  qt_add_executable(test-client tests/test_client.cpp)
  target_link_libraries(test-client PRIVATE electra_common Qt6::Test)
  add_test(NAME client-contract COMMAND test-client -platform offscreen)
  qt_add_executable(test-desktop tests/test_desktop.cpp src/admin_window.cpp src/admin_window.h src/user_window.cpp src/user_window.h)
  target_link_libraries(test-desktop PRIVATE electra_common Qt6::Charts Qt6::Test)
endif()
install(TARGETS electra-user electra-admin RUNTIME DESTINATION bin)
