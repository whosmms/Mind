# 环境版本记录（统一规范§5、§22；原设计§2.2、§12 M0）

检测与验证日期：2026年10月24日。本文件保留团队冻结基线和此前记录；**当前这台电脑的结果见文末“本机复现实测”**。不能把任一台的结果当作三机均通过。

## 团队冻结基线及此前记录
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

## 本机复现实测（2026-10-24，DEV-20261024-007）

仓库：`https://github.com/whosmms/Mind`；本地副本：`D:/mind/app`。
源代码验证基线：`83a144127bd87812e97a3317deb42b5abce223f7`。
本次由AI助手协助安装和复验；用户在同步时已明确认领**乙**，本机结果归入乙的环境复现，
仍不代甲、丙签名。安装阶段与同步阶段分别见DEV-20261024-007/008。
本批环境脚本与记录通过 `feature/b/M0-environment-sync` 同步，待甲/丙复核后合入main；
业务源码与根CMake配置未修改，源基线与本批文档/脚本提交应分开理解。

### 实际安装与路径

| 组件 | 本机实际版本 | 实际路径或来源 | 状态 |
|---|---|---|---|
| Windows | Windows 11，10.0.26200，x86_64 | 本机系统 | 已核对 |
| Qt Kit | **6.8.3 / win64_mingw** | `D:/mind/.tools/Qt/6.8.3/mingw_64` | 已安装并构建验证 |
| MinGW/g++ | **13.1.0**，x86_64-posix-seh-rev1 | `D:/mind/.tools/Qt/Tools/mingw1310_64/bin` | 已验证，未使用旧8.1.0 |
| CMake | **3.30.5** | `D:/mind/.tools/Qt/Tools/CMake_64/bin` | 已验证 |
| Ninja | **1.12.1** | `D:/mind/.tools/Qt/Tools/Ninja` | 已验证 |
| Qt模块 | Core/Gui/Widgets/Network/Svg/Concurrent/Test均为6.8.3 | 官方qtbase、qtsvg、qttools、qttranslations归档 | 七模块配置探针通过 |
| Qt Creator | **20.0.2** | `D:/mind/.tools/Qt/Tools/QtCreator/bin/qtcreator.exe` | 已打开工程、配置debug预设、构建并启动程序 |
| 调试器 | MinGW套件自带GDB | `D:/mind/.tools/Qt/Tools/mingw1310_64/bin/gdb.exe` | IDE已检测；交互断点调试未在本次验收 |
| clang-format | **19.1.7** | `D:/mind/.tools/formatter/clang_format/data/bin` | 工具正常，现有源码格式检查未通过，见遗留 |
| Git | 2.51.0.windows.1 | 现有Git安装 | 克隆与本地检查通过 |
| 安装辅助工具 | aqtinstall 3.3.0 | 独立放在 `D:/mind/.tools/aqt-python` | 未改全局Python环境 |
| 格式工具发行来源 | clang-format 19.1.7 Windows wheel | PyPI固定版本，可核对发行来源 | 非编译器，不替换MinGW |

Qt、MinGW、CMake、Ninja、Qt Creator及SDK工具从Qt官方存档下载，aqt保持SHA-256校验开启，未使用跳过哈希参数。Qt Creator自身携带的Qt运行库与项目Qt6.8.3互相独立；IDE版本变化不等于更改项目Kit。

以前记录中“Qt Creator/qttools/qtsvg未安装”和“Network随M5补装”不适用于本次本机复现：这些依赖现已安装，其中Network已包含在qtbase中。真实DeepSeek客户端仍属于后续M5，本次没有API调用。

### 如何使用

最方便的本机入口放在资料根目录 `D:/mind`：

- 双击 `打开QtCreator.cmd`：用本项目独立设置打开现有CMake工程。
- 双击 `打开开发终端.cmd`：打开只在当前终端生效的冻结开发环境。

这两个入口是当前电脑的便捷启动文件，不在源码仓库内。仓库内可复用脚本位于tools；使用PowerShell 7执行：

```powershell
# 在源码根目录操作；点后有一个空格，表示在当前终端载入环境。
. .\tools\进入开发环境.ps1

# 校验冻结版本，依次完成Debug和Release构建、CTest；日志落在忽略目录。
.\tools\验证环境.ps1

# 使用独立Qt Creator配置与本项目预设打开工程。
.\tools\打开QtCreator.ps1
```

默认脚本布局为“源码仓库父目录下的 `.tools/Qt` 与 `.tools/formatter`”。其他磁盘可保留相同相对布局，或通过脚本 `-ToolsRoot` 指定包含Qt和formatter子目录的工具根。旧的 `D:/Qt` 布局仍可手动设置QT_KIT_DIR后使用原CMake命令，不能机械把它当作本脚本的同一种目录结构。

脚本只改变当前进程环境，不修改永久PATH；关闭开发终端即恢复原环境。Qt Creator使用 `.tools/qtcreator-profile` 独立个人设置，自动生成的仓库 `.qtcreator/` 已加入.gitignore。进入IDE前会移除应用Qt插件路径，避免把Qt6.8.3插件强塞给IDE自身运行库。

### 本次实际验证

| 检查 | 本机结果 | 证据 |
|---|---|---|
| 四项冻结版本 | Qt6.8.3、GCC13.1.0、CMake3.30.5、Ninja1.12.1完全匹配 | `build/environment/validation.json` |
| Debug配置、构建、测试 | 返回0，3个CTest套件全部通过 | `build/environment/validation.log`、`build/debug/Testing/Temporary/LastTest.log` |
| Release配置、构建、测试 | 返回0，3个CTest套件全部通过 | 同上及release的CTest日志 |
| IDE | 已打开现有工程并配置debug预设；构建和运行入口可用，出现“未命名导图 - 灵芽思维”窗口 | 本次桌面检查；IDE个人配置与CMakeCache路径核对 |
| 七个Qt模块 | EXACT 6.8.3配置和探针构建成功；SVG与QtConcurrent探针通过 | `.tools/evidence/probe`与`modules-and-tls.json` |
| TLS | supportsSsl=true；可用后端含schannel；未发真实网络请求 | `.tools/evidence/modules-and-tls.json` |
| M0部署目录 | `app/staging/m0`，windeployqt --release成功 | `.tools/evidence/windeployqt.txt` |
| 不含Qt/MinGW的PATH启动 | 部署程序在本机offscreen模式持续运行4秒后结束 | `.tools/evidence/staging-startup-error.txt`及本次执行结果 |
| 格式检查 | 工具可用，但既有源码存在多处风格差异，返回1 | `build/environment/format-check.txt` |
| 其他两台电脑、完整GUI点击、真实API、安装器 | 本次未执行 | 不能宣称三机或M0全量验收通过 |

首次文件测试出现5个临时文件相关失败，写入目标位于受限执行环境的默认Temp；显式指定 `build/environment/temp` 后两个配置全部通过。环境验证脚本在测试期间设置TEMP/TMP并在结束后还原，未通过修改FileService或放松校验掩盖失败。

CMake显示未找到可选WrapVulkanHeaders，当前Widgets构建不依赖它且配置成功。windeployqt提示未找到dxcompiler/dxil和Qt bin内运行库；后者已从正确MinGW目录收集。当前M0部署本机启动通过，仍需另一台无Qt机器验证；本次没有据此声称DirectX相关绘图能力已经验收。

### 空间与待办

本次实际文件大小统计：工具与缓存约1.663 GiB，源码、双配置构建和M0部署约0.152 GiB，原设计资料与检查文件约0.052 GiB；工作区这些主要目录合计约**1.87 GiB**。这是文件逻辑大小，不是精确簇占用，后续更新和功能增长会增加；建议继续保留10至15 GB余量。统计证据为 `.tools/evidence/disk-usage.json`。

待办：另两台按相同冻结版本执行验证并由成员签字；乙审阅本次环境脚本；现有源码格式另开独立修改；实际断点调试、完整鼠标与输入法、另一台干净机器部署和正式安装包仍分别验收。
