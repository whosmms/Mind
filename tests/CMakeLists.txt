# 单元测试（统一规范§19.1、原设计§3）：测试链接对应层，不把窗口代码塞进核心库。
# tst_domain与tst_projectjson只依赖lingya_core；tst_commands依赖lingya_appcore（QUndoStack）。
# tst_layout、tst_ai、tst_ui随M2-M5对应模块实现后补充。
foreach(tst_name tst_domain tst_projectjson)
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
