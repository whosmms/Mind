# 单元测试（统一规范§19.1、原设计§3）：测试链接对应层，不把窗口代码塞进核心库。
# tst_domain与tst_projectjson只依赖lingya_core；tst_commands依赖lingya_appcore（QUndoStack）；
# tst_ui为UI层测试（快捷键与文件打开行为），链接窗口与图元源文件。
foreach(tst_name tst_domain tst_projectjson tst_check tst_review tst_fileservice tst_recovery tst_layout tst_session)
    add_executable(${tst_name} ${tst_name}.cpp)
    target_link_libraries(${tst_name} PRIVATE lingya_core Qt6::Test)
    target_compile_definitions(${tst_name} PRIVATE
        FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
    if(MSVC)
        target_compile_options(${tst_name} PRIVATE /utf-8)
    endif()
    add_test(NAME ${tst_name} COMMAND ${tst_name})
endforeach()

add_executable(tst_commands tst_commands.cpp)
target_link_libraries(tst_commands PRIVATE lingya_appcore Qt6::Test)
if(MSVC)
    target_compile_options(tst_commands PRIVATE /utf-8)
endif()
add_test(NAME tst_commands COMMAND tst_commands)

add_executable(tst_tasks tst_tasks.cpp)
target_link_libraries(tst_tasks PRIVATE lingya_appcore Qt6::Test)
if(MSVC)
    target_compile_options(tst_tasks PRIVATE /utf-8)
endif()
add_test(NAME tst_tasks COMMAND tst_tasks)

add_executable(tst_nodecommands tst_nodecommands.cpp)
target_link_libraries(tst_nodecommands PRIVATE lingya_appcore Qt6::Test)
if(MSVC)
    target_compile_options(tst_nodecommands PRIVATE /utf-8)
endif()
add_test(NAME tst_nodecommands COMMAND tst_nodecommands)

add_executable(tst_aicommands tst_aicommands.cpp)
target_link_libraries(tst_aicommands PRIVATE lingya_appcore Qt6::Test)
if(MSVC)
    target_compile_options(tst_aicommands PRIVATE /utf-8)
endif()
add_test(NAME tst_aicommands COMMAND tst_aicommands)

add_executable(tst_flow tst_flow.cpp)
target_link_libraries(tst_flow PRIVATE lingya_appcore Qt6::Test)
if(MSVC)
    target_compile_options(tst_flow PRIVATE /utf-8)
endif()
add_test(NAME tst_flow COMMAND tst_flow)

add_executable(tst_charts tst_charts.cpp)
target_link_libraries(tst_charts PRIVATE lingya_appcore Qt6::Test)
if(MSVC)
    target_compile_options(tst_charts PRIVATE /utf-8)
endif()
add_test(NAME tst_charts COMMAND tst_charts)

add_executable(tst_ai tst_ai.cpp)
target_link_libraries(tst_ai PRIVATE lingya_appcore Qt6::Test)
if(MSVC)
    target_compile_options(tst_ai PRIVATE /utf-8)
endif()
add_test(NAME tst_ai COMMAND tst_ai)

add_executable(tst_secret tst_secret.cpp)
target_link_libraries(tst_secret PRIVATE lingya_appcore Qt6::Test)
if(MSVC)
    target_compile_options(tst_secret PRIVATE /utf-8)
endif()
add_test(NAME tst_secret COMMAND tst_secret)

# 甘特视图依赖 QGraphicsView（Qt Widgets），单独链接 Widgets
add_executable(tst_ganttview tst_ganttview.cpp)
target_link_libraries(tst_ganttview PRIVATE lingya_appcore Qt6::Test Qt6::Widgets)
if(MSVC)
    target_compile_options(tst_ganttview PRIVATE /utf-8)
endif()
add_test(NAME tst_ganttview COMMAND tst_ganttview)

add_executable(tst_ui tst_ui.cpp
    ${CMAKE_SOURCE_DIR}/src/app/MainWindow.h
    ${CMAKE_SOURCE_DIR}/src/app/MainWindow.cpp
    ${CMAKE_SOURCE_DIR}/src/app/MainWindowFlow.cpp
    ${CMAKE_SOURCE_DIR}/src/app/MainWindowOffline.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ReviewPage.cpp
    ${CMAKE_SOURCE_DIR}/src/app/RecoveryDialog.h
    ${CMAKE_SOURCE_DIR}/src/app/RecoveryDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/LoginDialog.h
    ${CMAKE_SOURCE_DIR}/src/app/LoginDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/UserFilesDialog.h
    ${CMAKE_SOURCE_DIR}/src/app/UserFilesDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/graphics/NodeItem.h
    ${CMAKE_SOURCE_DIR}/src/graphics/NodeItem.cpp
    ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
target_link_libraries(tst_ui PRIVATE lingya_appcore Qt6::Test Qt6::Widgets)
if(MSVC)
    target_compile_options(tst_ui PRIVATE /utf-8)
endif()
add_test(NAME tst_ui COMMAND tst_ui)

# 复习页是 QWidget：单独链接 Widgets 与 ui 源文件
add_executable(tst_reviewpage tst_reviewpage.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ReviewPage.h
    ${CMAKE_SOURCE_DIR}/src/ui/ReviewPage.cpp)
target_link_libraries(tst_reviewpage PRIVATE lingya_appcore Qt6::Test Qt6::Widgets)
if(MSVC)
    target_compile_options(tst_reviewpage PRIVATE /utf-8)
endif()
add_test(NAME tst_reviewpage COMMAND tst_reviewpage)
# 检查助手：规则映射（CheckAssistant 在 appcore）+ 问题侧栏（也在 appcore）
add_executable(tst_checkassistant tst_checkassistant.cpp)
target_link_libraries(tst_checkassistant PRIVATE lingya_appcore Qt6::Test Qt6::Widgets)
if(MSVC)
    target_compile_options(tst_checkassistant PRIVATE /utf-8)
endif()
add_test(NAME tst_checkassistant COMMAND tst_checkassistant)
# 提示模板：纯服务，链接 appcore 即可
add_executable(tst_promptlibrary tst_promptlibrary.cpp)
target_link_libraries(tst_promptlibrary PRIVATE lingya_appcore Qt6::Test)
# 回退文本与 qrc 模板必须一致：用例需要读取仓库内的模板原文做比对
target_compile_definitions(tst_promptlibrary PRIVATE LINGYA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
if(MSVC)
    target_compile_options(tst_promptlibrary PRIVATE /utf-8)
endif()
add_test(NAME tst_promptlibrary COMMAND tst_promptlibrary)
# 复习卡片命令（X01/L-017 自评写回）：依赖 lingya_appcore（QUndoStack）
add_executable(tst_reviewcommands tst_reviewcommands.cpp)
target_link_libraries(tst_reviewcommands PRIVATE lingya_appcore Qt6::Test)
if(MSVC)
    target_compile_options(tst_reviewcommands PRIVATE /utf-8)
endif()
add_test(NAME tst_reviewcommands COMMAND tst_reviewcommands)

# DeepSeek 客户端：本机桩服务器测试，需要 Network
add_executable(tst_deepseekclient tst_deepseekclient.cpp)
target_link_libraries(tst_deepseekclient PRIVATE lingya_appcore Qt6::Test Qt6::Network)
if(MSVC)
    target_compile_options(tst_deepseekclient PRIVATE /utf-8)
endif()
add_test(NAME tst_deepseekclient COMMAND tst_deepseekclient)

# AI 面板：需要 appcore（AiService/FakeAiProvider）与 Widgets
add_executable(tst_aipanel tst_aipanel.cpp)
target_link_libraries(tst_aipanel PRIVATE lingya_appcore Qt6::Test Qt6::Widgets)
if(MSVC)
    target_compile_options(tst_aipanel PRIVATE /utf-8)
endif()
add_test(NAME tst_aipanel COMMAND tst_aipanel)

# 统一以 offscreen 跑 ctest（规范§19.1：自动测试不得在开发机桌面弹窗）。
# 用例只被平台插件拖住时，表现是弹模态「no Qt platform plugin」框并挂死，命令行里看不到任何输出；
# 所以平台与插件根目录都由这里定死，不依赖外层终端变量：
#   QT_QPA_PLATFORM=offscreen  前台就不会出现真窗口
#   QT_PLUGIN_PATH=<build>     测试 exe 在 build/<cfg>/tests/，而 platforms\ 在 build/<cfg>/，
#                              不指根目录的话 Qt 只能回落到 Qt 安装目录，换机就成败不定
# platforms\qoffscreen.dll 由 tools\准备运行依赖.ps1 一并部署。
# TIMEOUT 是兜底：万一将来又缺插件，用例在 300 秒内失败，而不是一直挂着等点「确定」。
get_property(tst_registered DIRECTORY PROPERTY TESTS)
if(tst_registered)
    set_tests_properties(${tst_registered} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=${CMAKE_BINARY_DIR}"
        TIMEOUT 300)
endif()
