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
