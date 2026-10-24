# 环境版本记录（统一规范§5、§22；原设计§2.2、§12 M0）

检测与验证日期：2026年10月24日。本表为**实际安装并验证的结果**，非推荐值。
安装方式：aqtinstall v3.3.0 从 Qt 官方存档（download.qt.io，与官方在线安装器同源）下载；
安装目录 `D:\Qt`（官方来源，符合统一规范§5.2“工具必须来自官方或可核对的发行来源”）。

## 已冻结的团队基线（乙待按此复现确认）

| 组件 | 实际版本 | 来源 | 验证结果 |
|---|---|---|---|
| Windows版本与架构 | Windows 11（内核10.0.26200）x86_64 | 系统信息 | cmake/ctest/应用均在此环境运行 |
| Qt完整版本 | **6.8.3**（win64_mingw，qtbase归档） | Qt官方存档经aqtinstall | find_package成功；构建、测试、应用启动通过 |
| Qt模块 | Core/Gui/Widgets/Test（qtbase内置）；Network随M5补装 | 同上 | 构建目标全部链接成功 |
| Kit名称 | win64_mingw | 官方存档 | — |
| MinGW/g++完整版本 | **13.1.0**（x86_64-posix-seh-rev1, MinGW-Builds，符合官方基线13.1） | Qt官方存档 tools_mingw1310 | Debug与Release全量编译0错误 |
| CMake完整版本 | **3.30.5** | Qt官方存档 tools_cmake | preset配置成功（要求≥3.21） |
| Ninja完整版本 | **1.12.1** | Qt官方存档 tools_ninja | 构建执行正常 |
| Git完整版本 | **2.51.0.windows.1** | Git官方 | 仓库操作正常；成员身份未配置（首次提交前须设置） |
| clang-format | 19.1.7（目标值） | 未安装 | **未核验**——格式检查未执行，见下 |
| Qt Creator | 未安装 | — | 编码可用命令行；成员按需安装后回填 |
| TLS后端 | 待部署实测 | — | 未核验（M5联网前必须确认） |

## 构建与测试执行记录（2026-10-24）

| 命令 | 结果 |
|---|---|
| `cmake --preset debug` | 返回0，配置成功（6.1秒） |
| `cmake --build --preset debug --parallel 4` | 返回0，4个目标（lingya_core/liblingya_appcore/lingya/3测试）0错误 |
| `ctest --preset debug --no-tests=error` | **3套件39用例全部通过**（domain 15、projectjson 16、commands 8） |
| `cmake --preset release` + 构建 + `ctest --preset release` | 返回0，**Release配置同样39用例全部通过** |
| `lingya.exe` 启动验证 | 窗口程序持续运行4秒以上无崩溃（QSS、qrc资源加载正常），手动关闭 |
| `clang-format --dry-run --Werror` | **未执行**（clang-format未安装；格式对齐留待工具安装后一次性处理，见遗留项） |

测试运行注意事项：ctest/测试可执行文件运行时需将 `D:/Qt/6.8.3/mingw_64/bin` 加入PATH
（DLL加载），或后续为测试目标配置部署步骤（统一规范§19.2的CI化时处理）。

## 遗留项

- clang-format 19.1.7 未安装：M0代码未做格式对齐检查（`-i`格式化会产生大范围diff，
  建议在首次提交后作为独立格式提交处理，不与业务修改混合）。
- Qt Creator、windeployqt（qttools归档）、qtsvg 未安装：Release最小部署验证（统一规范§4
  第10项）待qttools补装后进行。
- 本表冻结值需乙、丙在本机复现`cmake --preset debug`与`ctest --preset debug`后签字确认
  （统一规范§5.1“甲丙复现后冻结”）。
