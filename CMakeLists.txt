# 灵芽思维（LingyaMind）统一构建
# 依据：三人协作开发统一规范§5.3（预设）、§6.6（显式源文件、AUTOMOC/AUTORCC）、§7.1（分层）。
# 分层：lingya_core只含不依赖界面的领域、序列化和纯规则代码，仅链接Qt Core；
#       lingya_appcore为应用支持层（文档、命令历史、AI契约），含QUndoStack故链接Qt Widgets；
#       可执行目标lingya承载窗口与场景。测试只链接对应层，不把窗口代码塞入核心库。
# 说明：QUndoStack/QUndoCommand位于Qt Widgets模块（Qt 6.8官方模块划分）。
cmake_minimum_required(VERSION 3.21)
project(LingyaMind VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Qt模块仅启用规范§5.1列出的集合；M0使用Core、Widgets、Test，Network随M5接入
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Widgets Test)

# 核心库：领域、校验器、JSON序列化与文件服务（纯规则，无界面）
add_library(lingya_core STATIC
    src/domain/Domain.h
    src/domain/Limits.h
    src/domain/TitleRules.h
    src/domain/ProjectFactory.h
    src/domain/ProjectFactory.cpp
    src/domain/ProjectValidator.h
    src/domain/ProjectValidator.cpp
    src/services/ProjectJson.h
    src/services/ProjectJson.cpp
    src/services/FileService.h
    src/services/FileService.cpp
    src/services/RecoveryService.h
    src/services/RecoveryService.cpp
    src/services/SettingsService.h
    src/services/SettingsService.cpp
    src/services/SessionService.h
    src/services/SessionService.cpp
    src/services/UserFileService.h
    src/services/UserFileService.cpp
    src/features/CheckService.h
    src/features/CheckService.cpp
    src/features/ReviewService.h
    src/features/ReviewService.cpp
    src/features/ReviewSession.h
    src/features/ReviewSession.cpp
    src/layout/TreeLayout.h
    src/layout/TreeLayout.cpp
)
target_include_directories(lingya_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(lingya_core PUBLIC Qt6::Core)
if(MSVC)
    target_compile_options(lingya_core PUBLIC /utf-8)
endif()

# 应用支持层：唯一文档、命令历史与AI契约（QUndoStack需要Qt Widgets）
add_library(lingya_appcore STATIC
    src/document/ProjectDocument.h
    src/document/ProjectDocument.cpp
    src/commands/CommandService.h
    src/commands/CommandService.cpp
    src/commands/SnapshotCommand.h
    src/commands/SnapshotCommand.cpp
    src/commands/TaskCommands.h
    src/commands/TaskCommands.cpp
    src/commands/NodeCommands.h
    src/commands/NodeCommands.cpp
    src/ai/AiTypes.h
    src/ai/IAiProvider.h
    src/ai/FakeAiProvider.h
    src/ai/FakeAiProvider.cpp
    src/ai/AiValidator.h
    src/ai/AiValidator.cpp
    src/ai/SecretStore.h
    src/ai/SecretStore.cpp
    src/ai/AiService.h
    src/ai/AiService.cpp
    src/ai/PromptLibrary.h
    src/ai/PromptLibrary.cpp
    src/charts/TaskTableModel.h
    src/charts/TaskTableModel.cpp
    src/charts/GanttLayout.h
    src/charts/GanttLayout.cpp
    src/charts/GanttView.h
    src/charts/GanttView.cpp
)
target_link_libraries(lingya_appcore PUBLIC lingya_core Qt6::Core Qt6::Widgets)
if(WIN32)
    # SecretStore 使用 Windows 凭据管理器（CredWriteW/CredReadW/CredDeleteW）
    target_link_libraries(lingya_appcore PUBLIC Advapi32)
endif()
if(MSVC)
    target_compile_options(lingya_appcore PUBLIC /utf-8)
endif()

# 可执行目标：窗口装配与画布图元
add_executable(lingya
    src/main.cpp
    src/app/MainWindow.h
    src/app/MainWindow.cpp
    src/app/RecoveryDialog.h
    src/app/RecoveryDialog.cpp
    src/app/LoginDialog.h
    src/app/LoginDialog.cpp
    src/app/UserFilesDialog.h
    src/app/UserFilesDialog.cpp
    src/graphics/NodeItem.h
    src/graphics/NodeItem.cpp
    resources/resources.qrc
)
target_link_libraries(lingya PRIVATE lingya_appcore Qt6::Widgets)
if(MSVC)
    target_compile_options(lingya PRIVATE /utf-8)
endif()
if(WIN32)
    set_target_properties(lingya PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# 测试：tst_domain与tst_projectjson只依赖核心库；tst_commands依赖应用支持层
enable_testing()
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
