# 第三方组件清单（统一规范§22、§23）

维护人：乙。每次新增依赖先在此登记版本、来源与授权，再进 CMake；
发布前按本清单核对需随包提供的文件。首次M0为静态记录；后续本机部署小验证见environment.md，跨机器与最终许可随包核对仍待执行。

| 组件 | 版本 | 来源 | 授权 | 用途 | 随包义务 |
|---|---|---|---|---|---|
| Qt 6 | **6.8.3**（win64_mingw，2026-10-24三机复现后冻结） | Qt官方安装器/官方存档 | **LGPL-3.0-only**（动态链接；Qt 同时提供 GPL-2.0/GPL-3.0 与商业许可，本项目选 LGPL-3.0-only） | 全部界面、数据与网络基础 | 随包提供LGPL-3.0许可全文、来源说明与源码获取方式；保留动态链接，不静态编译Qt |
| Qt模块：Core / Gui / Widgets / Network / Test（**实际使用**）；Svg / Concurrent 未使用 | 随Qt 6.8.3 | 同上 | 同上 | `CMakeLists.txt:17` 已 `find_package(Qt6 6.8 REQUIRED COMPONENTS Core Widgets Network Test)`；`lingya_appcore` 链接 `Qt6::Network`（M5 的 DeepSeekClient/AiService 合入后启用），`tst_deepseekclient` 亦链接。**2026-11-03 更正**：原文「Network 随M5接真实请求时加入find_package与链接」已过期，实际早已在链 | 同上；新增模块先登记（统一规范§5.1） |
| MinGW 64位（Qt官方配套） | **13.1.0**（`D:\Qt\Tools\mingw1310_64`） | Qt官方安装器 | GPL+运行时例外 | 编译与调试工具链 | 开发工具，不随应用分发；部署运行库经windeployqt收集 |
| **MinGW运行时** `libgcc_s_seh-1.dll`、`libstdc++-6.dll` | 随MinGW 13.1.0 | 同上 | GPL-3.0-or-later **WITH GCC Runtime Library Exception 3.1** | 应用运行必需 | **随包分发**；运行时例外已覆盖二进制分发义务，文本见 `D:\Qt\Tools\mingw1310_64\licenses\gcc\COPYING.RUNTIME` |
| **MinGW运行时** `libwinpthread-1.dll` | 1.0.0.0 | 同上 | **MIT** | 线程运行库 | **随包分发**；文本见 `D:\Qt\Tools\mingw1310_64\licenses\winpthreads\COPYING` |
| **Microsoft** `D3Dcompiler_47.dll` | 10.0.26100.9457 | Windows SDK，经 windeployqt 收集 | Microsoft 可再分发组件 | Qt Gui 的 D3D 着色器编译 | **原清单遗漏，2026-11-03 补登**；本机工具链未带许可文本，正式发布包需补 Microsoft 声明 |
| Qt 插件：`platforms/qwindows`、`imageformats/qgif`+`qico`+`qjpeg`、`styles/qmodernwindowsstyle`、`tls/qcertonlybackend`+`qschannelbackend`、`networkinformation/qnetworklistmanager`、`generic/qtuiotouchplugin` | 随Qt 6.8.3 | Qt官方 | LGPL-3.0-only（本项目选用） | 平台/图像/样式/TLS/网络状态/触摸 | 与 Qt 库同等义务；逐个文件的 `licenseConcluded` 已核 SPDX SBOM |
| CMake | ≥3.21，与Qt要求取高者 | Kit配套或官方发行 | BSD-3 | 构建系统 | 开发工具，不随应用分发 |
| Ninja | Kit配套版本 | 同上 | Apache-2.0 | 构建执行器 | 开发工具，不随应用分发 |
| Inno Setup | 未安装（M6 仍未落地） | jrsoftware.org | Inno Setup License | 打包安装程序（M6） | 工具尚未使用，正式安装包未生成；启用时再登记实际版本 |
| clang-format | 19.1.7 | LLVM官方发行 | Apache-2.0（LLVM） | 代码格式化（统一规范§9.1） | 开发工具，不随应用分发 |

说明：

- 本项目不引入上述清单之外的任何第三方库；新增必须固定版本与校验值并经乙登记。
- 应用自身图标与提示模板为项目原创资源，不属第三方组件。
- 各组件的完整版本号在环境冻结后回填 docs/environment.md；本文件不虚构未安装版本。
- **冻结依据（2026-10-24）**：Qt 6.8.3 + MinGW 13.1.0 + CMake 3.30.5 + Ninja 1.12.1 + clang-format 19.1.7，
  甲乙丙三机同版本、同提交各自构建与测试通过，登记见 `docs/environment.md` 三段环境记录；
  此后不再接受“随Qt 6.8”这类浮动表述，替换版本须走全队一次性迁移（统一规范§5.1）。
- Qt补丁号来自实际安装与`qmake --version`核对，不按文档推测。


## 甲本机开发工具安装补充（2026-10-24，H-002）

Qt6.8.3、MinGW13.1.0、CMake3.30.5、Ninja1.12.1取自Qt官方存档；Qt Creator20.0.2同源。
clang-format19.1.7本机来源为可核对的PyPI Windows wheel，许可文件保留在安装目录。
aqtinstall3.3.0及其Python依赖只用于隔离安装，不链接应用、不随应用分发。
本次未新增应用第三方依赖；既有表中的推荐版本以environment.md当前实际基线为准。
最终分发的Qt模块、插件、运行库、许可证与来源说明由乙在M6逐项核对。


## M6 随包核对（2026-11-03，乙）

上一节末句「最终分发的Qt模块、插件、运行库、许可证与来源说明由乙在M6逐项核对」即本节。**已核对，未闭合**——理由见本节末。

### 实测方法（证据目录 `docs/evidence/20261103-乙M6部署与许可/`）

空目录只放 `lingya.exe`（2,461,477 字节）→ `windeployqt --release --compiler-runtime --no-translations`，基线提交 `b80dc0d`。

结果：**18 个文件、39,246,917 字节（37.43 MB）**，逐个附 SHA256 前 16 位，见 `随包文件清单.txt`。分布：根目录 9、imageformats 3、tls 2、platforms/styles/networkinformation/generic 各 1。

### 三项核对结论

1. **依赖闭合（pass）**：`objdump -p` 读 `lingya.exe` 及 `Qt6Core/Gui/Network/Widgets.dll` 的 PE 导入表，非系统依赖全部在清单内——含仅被 Qt6Core 间接导入、exe 本身不导入的 `libwinpthread-1.dll`（只看 exe 的导入会漏掉它）。
2. **部署自洽（pass）**：`PATH` 仅含 `C:/WINDOWS/system32` 时启动，8 秒存活、退出码 124、无平台插件错误，复跑两次一致。另用 `QT_ASSUME_STDERR_HAS_CONSOLE=1 QT_DEBUG_PLUGINS=1` 取加载日志：插件路径出现部署目录 **22 次**、Qt 安装目录 `6.8.3` **0 次**——证明没有回落到 Qt6 内置的安装前缀。这一步必要，因为 GUI 子系统的程序 stderr 默认为空，**「stderr 空」不能证明「无错误」**。
3. **许可来源可核（pass）**：Qt 的 13 个二进制逐个对上 `D:/Qt/6.8.3/mingw_64/sbom/qtbase-6.8.3.spdx.json`（SPDX-2.3，2025-03-17）的 `licenseConcluded`，均为 `LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only`，本项目按 **LGPL-3.0-only 动态链接**使用。不再凭印象写「LGPLv3」。

### 启动阶段实际加载的插件

`platforms/qwindows` + `imageformats/qgif`/`qico`/`qjpeg` + `styles/qmodernwindowsstyle`。`tls/` 与 `networkinformation/` 为**按需加载**，本次启动未触发——所以 TLS 是否可用，本次实测**证明不了**。

### 随包义务待办（M6 未闭合的真实原因）

- [ ] 部署包内附 LGPL-3.0 全文与本清单（LGPLv3 §4）
- [ ] 提供 Qt 源码获取方式（LGPLv3 §4(d)）
- [ ] **项目自身 LICENSE 未定**：仓库内无任何 `LICENSE`/`COPYING`/`NOTICE` 文件，属三人共有决策，乙不单方面指定
- [ ] `D3Dcompiler_47.dll` 的 Microsoft 可再分发声明文本（本机工具链未带）
- [ ] 安装器未落地，正式安装包未生成

### 未覆盖，不得据此宣告 M6 通过

- **干净 Windows 机器**未验：本节全部是「本机精简 PATH」，不是另一台机器
- `tls/qschannelbackend` 未经真实 HTTPS 请求验证（需真实密钥，属 M5 未过项 L-028）
- 中文输入法、非 100% DPI、多显示器未验
- `windeployqt` 提示缺 `dxcompiler.dll`/`dxil.dll`（D3D12 着色器编译器）：Widgets 应用走 D3D11/软件渲染，本次启动无影响；若将来启用 Qt Quick/RHI 的 D3D12 后端需补
