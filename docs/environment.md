# 环境版本记录（统一规范§5、§22；原设计§12 M0）

更新日期：2026年10月24日。适用范围：Windows x86_64三人开发环境；当前成员甲takagi（Takagi-gif）、乙LH（whosmms）、丙wbccaidapkc（用户明确确认账号，真实姓名未提供）。

## 当前采用的基线与验证边界

Qt **6.8.3 win64_mingw**、MinGW **13.1.0**、CMake **3.30.5**、Ninja **1.12.1**，
与首次试点及用户提供图片一致。clang-format固定 **19.1.7**。每人必须按同一源码提交复现，
三机全部通过后才记为团队环境验收通过；单机结果不能代签。IDE版本记录在各自环境，编译用Kit保持一致。

本文件现含**甲、乙、丙三机的登记段**：甲段由甲takagi登记，乙段由乙LH登记，丙段由丙wbccaidapkc登记，互不代签。
此前未合入的错误分支把甲电脑写成乙电脑，现按H-002与DEV-20261024-011更正。
下文首次试点中的 `D:/Qt` 即乙本机安装路径；甲使用 `D:/mind/.tools/Qt`，二者不能混写为同一安装。

## 甲电脑的实际环境

| 项目 | 甲本机实际值 | 验证或边界 |
|---|---|---|
| Windows | Windows 11 x86_64（内核10.0.26200） | 本机运行环境 |
| Qt / Kit | 6.8.3 / win64_mingw | 官方存档，七模块探针通过 |
| Qt模块 | Core、Gui、Widgets、Network、Svg、Concurrent、Test | 已安装；应用仍只链接当前CMake声明的模块 |
| MinGW / g++ | 13.1.0（x86_64-posix-seh） | 官方配套工具 |
| CMake / Ninja | 3.30.5 / 1.12.1 | 与图片冻结值一致 |
| Qt Creator | 20.0.2 | 已实际打开、配置、构建、运行；不是Qt Kit版本号 |
| Git | 2.51.0.windows.1 | 沿用已有安装 |
| clang-format | 19.1.7 | 已安装；既有代码格式检查存在差异，未整体整理 |
| TLS | supportsSsl=true，后端含schannel | 探针通过；未调用真实DeepSeek |
| 工具根目录 | `D:/mind/.tools` | 个人路径，不写入共享CMake |
| IDE个人配置 | `.tools/qtcreator-profile` | 不提交；源码内个人`.qtcreator/`已忽略 |

安装来源：aqtinstall3.3.0从Qt官方存档取得Qt、MinGW、CMake、Ninja、Qt Creator及扩展组件，
保持下载SHA-256校验；clang-format19.1.7使用可核对的PyPI固定版本Windows wheel。
没有修改永久PATH或全局Python。工具及缓存不上GitHub。

### 日常进入与复验

在PowerShell 7中、仓库根目录运行：

```powershell
. .\tools\进入开发环境.ps1
.\tools\验证环境.ps1
.\tools\打开QtCreator.ps1
```

默认工具目录为源码父目录的`.tools`，内部包含`Qt/`、`formatter/`。目录不同可传`-ToolsRoot`，
或自行设置登记过的Kit和配套工具路径后用CMake预设。严禁借用Anaconda Qt/Ninja或MinGW8.1.0。
环境脚本只改变当前进程；IDE启动前移除应用Qt插件路径，以免与IDE自己的Qt运行库冲突。

`验证环境.ps1`严格比对冻结版本，配置和构建debug/release，再执行非零CTest。
自动测试使用offscreen；TEMP/TMP暂指到`build/environment/temp`，结束后恢复。
证据输出到忽略目录`build/environment/validation.json`、`validation.log`及两配置的CTest日志。

两个本机CMD入口保留UTF-8无BOM、CRLF，路径在资料根目录；曾因LF被cmd错误解析，修复及实际启动证据见
DEV-20261024-010与`.tools/evidence/launcher-diagnosis/`。本次切分支已先备份并恢复三个脚本，CMD内容不变。

### 验证批次与未完成项

| 批次 | 源码依据 | 实际结果及证据 |
|---|---|---|
| 安装后首验（历史） | `83a144127bd87812e97a3317deb42b5abce223f7` | debug/release各3个CTest套件通过；原日志备份保留 |
| 旧上传前复验（历史、上传已撤回） | `1f34d8cba3212d1220cfe084cdafa5baecdd8d74` | 隔离目录sync-debug/sync-release各3套件通过；`.tools/evidence/github-sync/build-and-test.log` |
| 本次甲身份修正复验 | `3650d4738986b36c1edab6306e82fd0fb167af6a`及本分支环境脚本 | 结果见DEV-20261024-011 / INT-20261024-004；不能套用旧3套件结果 |
| Qt模块与TLS | 安装后的依赖探针 | `.tools/evidence/modules-and-tls.json`；未发真实请求 |
| 早期M0部署小验证（历史） | 初次安装时构建产物 | windeployqt收集成功，本机精简PATH离屏运行4秒；未代表本次最新版部署或另一台电脑通过 |

尚需：三人在**同一最终提交**上复验并签收、完整人工输入法/键鼠/高DPI、断点调试、干净电脑部署与M6安装器。
既有源码格式差异单独处理。本次不重装环境、不改业务、不调用真实API。
历史工具与缓存逻辑大小约1.663 GiB，连同当时源码构建部署及资料约1.87 GiB；仅为当时统计，建议保留10–15 GB余量。

## 乙电脑的实际环境

登记日期：2026年10月24日。登记人：乙LH（本机执行，命令由AI协助）。首次试点的`D:\Qt`安装即在本机。

| 项目 | 乙本机实际值 | 验证或边界 |
|---|---|---|
| Windows | Windows 11 x86_64（计算机名XIAOXIN） | 本机运行环境 |
| Qt / Kit | 6.8.3 / win64_mingw，安装于`D:\Qt` | 官方存档（首次试点经aqtinstall取得，本次复用） |
| Qt模块 | Core、Gui、Widgets、Network、Svg、Concurrent、Test | 随既有安装提供；应用链接以CMake声明为准 |
| MinGW / g++ | 13.1.0（`D:\Qt\Tools\mingw1310_64`） | 官方配套工具 |
| CMake / Ninja | 3.30.5 / 1.12.1（`D:\Qt\Tools`） | 与冻结值一致 |
| Qt Creator | 未安装 | 本机以命令行与脚本开发；安装后回填 |
| Git | 2.51.0.windows.1 | 官方安装 |
| clang-format | 19.1.7（`D:\formatter\clang_format\data\bin`） | PyPI固定版本wheel，2026-10-24补装，不改全局Python |
| PowerShell | 7.6.8（官方ZIP解压于`D:\mind\.tools\pwsh7`，不改系统PATH） | GitHub官方发布，SHA-256校验一致 |
| TLS | 未单独探测 | 甲机探针已含schannel；本机M5联网前补探测 |

### 乙本机同提交复验与冻结签字（2026-10-24）

- 源码提交：main=`5efac3be2a4e97334bebea236320551f5b289ca4`，工作区干净。
- 命令：`tools/验证环境.ps1 -ToolsRoot D:\`（pwsh 7.6.8执行；脚本dot-source进入开发环境.ps1，校验五项冻结工具后双配置构建与非零CTest）。
- 结果：debug与release各6套件65用例全部通过，退出码0（QtTest Totals：19/16/9/8/7/6）。
- 证据：`build/environment/validation.json`（sourceCommit、versions、success字段）、`validation.log`；该目录在忽略清单，不入库。
- 签字：乙确认本机工具与§5.1冻结基线一致；三机验收整体通过仍需丙在本机复现（统一规范§5.1）。
- 全记录见开发日志[INT-20261024-005](开发日志.md)。
- **最终统一签收（2026-10-24）**：main=`4713a79839abefab3112d332dabce68578328e42`精确检出，双配置9套件105用例全过
  （Totals 19/16/13/10/16/8/7/7/9），退出码0；见开发日志[INT-20261024-009](开发日志.md)。
- **丙机同一提交签收（2026-10-24）**：丙wbccaidapkc在LAPTOP-RPV0EDEV对同一提交`4713a79`复现，
  双配置9套件105用例全过（Totals与乙机逐项一致）；见开发日志[INT-20261024-010](开发日志.md)。三机签收余甲。

## 丙电脑的实际环境

登记日期：2026年10月24日。登记人：丙wbccaidapkc（本机执行，命令由AI协助）。

| 项目 | 丙本机实际值 | 验证或边界 |
|---|---|---|
| Windows | Windows 11 x86_64（计算机名LAPTOP-RPV0EDEV，内核10.0.26200） | 本机运行环境 |
| Qt / Kit | 6.8.3 / win64_mingw，安装于`D:\Qt` | 官方存档（经aqtinstall取得） |
| Qt模块 | Core、Gui、Widgets、Test（qtbase）；Network/Svg随默认集提供 | 应用链接以CMake声明为准 |
| MinGW / g++ | 13.1.0（`D:\Qt\Tools\mingw1310_64`） | 官方配套工具 |
| CMake / Ninja | 3.30.5 / 1.12.1（`D:\Qt\Tools`） | 与冻结值一致 |
| Qt Creator | 未安装 | 本机以命令行与脚本开发；安装后回填 |
| Git | 2.55.0.windows.4 | 官方安装（比甲/乙的2.51.0略新，不影响构建） |
| clang-format | 19.1.7（`D:\formatter\clang_format\data\bin`） | PyPI固定版本wheel，2026-10-24补装，不改全局Python |
| PowerShell | 7.6.8（官方安装于`C:\Program Files\PowerShell\7`） | 用于运行环境脚本 |
| TLS | 未单独探测 | 本机M5联网前补探测 |

### 丙本机同提交复验与冻结签字（2026-10-24）

- 源码提交：main=`1aabab9`，工作区干净。
- 命令：`tools/验证环境.ps1 -ToolsRoot D:\`（pwsh 7.6.8执行；脚本dot-source进入开发环境.ps1，校验五项冻结工具后双配置构建与非零CTest）。
- 结果：debug与release各8套件86用例全部通过，退出码0（QtTest Totals：19/16/13/10/8/7/7/6）。
- 证据：`build/environment/validation.json`（sourceCommit、versions、success字段）、`validation.log`；该目录在忽略清单，不入库。
- 另：同一脚本在`feature/b/R04-file-management`（4618cfb）复跑为9套件全过，见该分支复核记录。
- 签字：丙确认本机工具与§5.1冻结基线一致；三机验收整体通过需甲、乙、丙在同一最终提交复验。

## 首次试点历史（保留原结果）

以下为原已合入记录；其中“未安装”“未核验”只描述当时试点，不能覆盖上面的甲本机新结果。

检测与验证日期：2026年10月24日。本表为**实际安装并验证的结果**，非推荐值。
安装方式：aqtinstall v3.3.0 从 Qt 官方存档（download.qt.io，与官方在线安装器同源）下载；
安装目录 `D:\Qt`（官方来源，符合统一规范§5.2“工具必须来自官方或可核对的发行来源”）。

## 首次试点工具记录（历史）

| 组件 | 实际版本 | 来源 | 验证结果 |
|---|---|---|---|
| Windows版本与架构 | Windows 11（内核10.0.26200）x86_64 | 系统信息 | cmake/ctest/应用均在此环境运行 |
| Qt完整版本 | **6.8.3**（win64_mingw，qtbase归档） | Qt官方存档经aqtinstall | find_package成功；构建、测试、应用启动通过 |
| Qt模块 | Core/Gui/Widgets/Test（首次验证）；Network随qtbase提供，M5才接入应用 | 同上 | 构建目标全部链接成功 |
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


## M0直接启动依赖修复（2026-10-24，H-004）

修复前build/debug仅含编译产物，未带Qt6Core/Gui/Widgets、MinGW运行库及qwindows.dll。
新增`tools/准备运行依赖.ps1`：用本Kit的objdump读取PE导入，再调用同Kit的windeployqt；
从配套MinGW补齐运行库并核对核心DLL与Windows平台插件。当前应用两个配置都引用无d后缀Qt，部署选release Qt库，
不是把C++ Debug构建改成Release。源码、JSON和系统PATH均不因此改变。

新增`tools/打开灵芽思维.ps1`：配置构建→准备依赖→启动窗口；已在运行则保留现有窗口。
本机CMD入口是`D:/mind/打开灵芽思维.cmd`，UTF-8无BOM、CRLF，PowerShell失败时返回非零；不进Git。
`验证环境.ps1`构建后也调用部署。干净构建目录不会再因只编译未准备依赖而被误当作可独立运行的成品。

本次INT-20261024-021：Debug/Release各4个CTest套件通过；精简PATH仅含Windows系统目录、移除Qt环境变量后，
两个配置均运行3秒，stderr为空，随后只结束测试自建且未编辑的进程。实际桌面已见窗口、中心主题与F2中文改名生效；
Release完整开发启动入口退出0，CMD的已有窗口分支退出0且stderr为空。

证据：build/environment/validation.json、cleanpath-startup.json与本机.tools/evidence的启动依赖修复验证.log、
灵芽启动入口Release.log、双击灵芽入口stdout/stderr；仅窗口截图进入docs/evidence，机器日志和DLL不上传。
受限工具环境曾使windeployqt的qtpaths子进程返回0xC0000142，普通本机执行环境成功；此为执行环境限制，未改安全设置。
首次按构建名传--debug还曾找不到平台插件，现通过PE实际导入选择类型，保留失败历史，不将失败记录写成成功。
本机运行通过不代表其他两台电脑或无Qt电脑已验收。


最终复验更新：本批随后合入团队main=c8ee59d，被测合并提交b3ffd3d5a97b64dd50c8107446d9c079e419be09，
Debug/Release各6套件通过（包含丙新增任务与检查测试）；日志为`.tools/evidence/启动修复合并后六套件验证.log`。
最新cleanpath-startup.json为合并后两配置复测结果。首次CMD启动也完成，退出0；windeployqt可选DXC及编译器搜索提示仍保留，
手动补齐的MinGW必需库与实际窗口均核验通过。早期4套件与已有窗口分支的零stderr记录均保留为历史，不冒充首次完整启动输出。


## 4713a79三机统一自动签收闭合（2026-10-24）

统一被测完整提交：`4713a79839abefab3112d332dabce68578328e42`。

| 成员与电脑 | Debug | Release | 记录与来源 |
|---|---|---|---|
| 甲takagi / TAKAGI | 9套件105项通过 | 9套件105项通过 | 本轮在精确detached HEAD干净工作树实测，INT-20261024-023；原脚本退出0，QtTest XML逐套核对0失败0跳过 |
| 乙LH / XIAOXIN | 9套件105项通过 | 9套件105项通过 | 乙本人既有同提交签收，INT-20261024-009 |
| 丙wbccaidapkc / LAPTOP-RPV0EDEV | 9套件105项通过 | 9套件105项通过 | 丙本人既有同提交签收，INT-20261024-010（丙报告、乙登记） |

甲本机：Windows 11 x86_64，内核10.0.26200.0；PowerShell7.6.5。
五项冻结工具实测一致：Qt6.8.3、g++13.1.0、CMake3.30.5、Ninja1.12.1、clang-format19.1.7。
实际运行原`tools/验证环境.ps1`，默认双配置构建、依赖准备、非零CTest均通过；未修改脚本或被测源码。
验证前后Git工作树均干净，validation.json的sourceCommit精确等于统一SHA；完成后返回最新main再登记结果。
为核对与乙丙一致的105项口径，另收集QtTest XML；数字含initTestCase/cleanupTestCase，不与纯测试函数数混用。
套件Totals依次为19/16/13/10/16/8/7/7/9，合计105，两个配置一致。

[甲机脱敏汇总](evidence/4713a79-甲机统一签收.json)仅含提交、工具版本、电脑标识、时间与逐套计数；原XML、validation.json/log和执行日志留在
甲本机`.tools/evidence/M0-4713a79-甲统一签收/`，不上传原始机器日志。此处没有在甲电脑代跑乙或丙。
本项统一签收已闭合；跨机部署、输入法预编辑、DPI三档与完整人工保存重开仍按IF-004暂缓。
后续代码变化应按变更范围回归，本记录不自动覆盖未来提交。
