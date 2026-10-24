# 验收记录（统一规范§19；原设计§12阶段门禁、§15测试目标）

维护人：丙（统一规范§2.2）。状态标记：[x]完成　[~]代码就绪待构建验证　[ ]未开始。
“静态自查”只证明文档与代码自洽，不能代替构建、测试或真实操作（统一规范§19.3）。

## M0 工程准备与纵向验证（当前阶段）

| 门禁产物 | 状态 | 证据 |
|---|---|---|
| 能启动的工程 | [x] | Debug与Release构建0错误；lingya.exe启动验证通过（运行4秒以上无崩溃）；环境冻结记录见environment.md |
| 环境版本记录 | [x] | environment.md（含统一规范§5.2要求的完整清单框架与AI检测注记） |
| 格式、换行、忽略配置 | [x] | .clang-format/.editorconfig/.gitattributes/.gitignore按统一规范§9逐字建立（静态核对） |
| 通用构建预设 | [x] | CMakePresets.json按§5.3建立（静态核对；未运行） |
| README | [x] | 根README.md（与代码、docs同步更新） |
| AGENTS.md | [x] | 根AGENTS.md（AI协助与新人入口，统一规范§24） |
| 最小.lingya样例 | [x] | tests/fixtures/minimal.lingya + bad-unknown-field + long-chinese（JSON静态解析通过，180字标题核对通过） |
| 共享类型及假AI响应 | [x] | src/ai/AiTypes.h、IAiProvider.h、FakeAiProvider（§10.4响应外形+3种失败模式；仅静态自查） |
| Domain最小子集、工厂、JSON | [x] | tst_domain 19用例+tst_projectjson 16用例全部通过（含任务日期、全类型往返幂等、拒绝场景） |
| 统一撤销命令最小闭环 | [x] | tst_commands 8用例+tst_ui快捷键用例通过（事务、撤销重做、失败不入栈、50步上限、QWindow按键路径）；修复BUG-010后快捷键真实验证 |
| 空窗口、资源、菜单连接 | [~] | MainWindow中文菜单、light.qss、NodeItem；exe启动验证通过；窗口内点击链路待人工复验 |
| 任务日期校验与测试框架起步 | [x] | ProjectValidator日期/依赖/里程碑规则经16个域用例覆盖；CTest双配置全过 |
| 纵向闭环（新建→改名→保存→关闭→重开） | [x] | m0VerticalSlice通过（保存→重开→校验一致）；真实点击链路待人工复验 |
| Release最小部署与另一台电脑启动 | [ ] | 统一规范§4第10项；无Qt环境，未启动 |
| 打包小验证 | [ ] | 同上；环境冻结后第一优先补做 |

首轮构建与测试（2026-10-24第三段，DEV-20261024-003）：工具链安装（Qt6.8.3/MinGW13.1/
CMake3.30.5/Ninja1.12.1，官方存档）；Debug与Release各39用例100%通过；lingya.exe启动验证；
第三段后续（DEV-20261024-007）：新增tst_ui（快捷键/文件打开），修复BUG-010/011，总计43用例；
构建期修复7项缺陷（BUG-001至BUG-007：contentRevision读写不对称、测试悬空引用、
颜色契约、qMin类型、using位置、maxSheets、QMap first顺序依赖）。

本轮规范对齐（2026-10-24第二段，静态自查通过，详见DEV-20261024-002与H-001）：
配置文件逐字对齐§9、预设对齐§5.3、构建分层对齐§7.1、namespace与常量命名对齐§8、
错误码对齐§10.3（映射见decisions.md D-M0-11）、docs结构对齐§6（协作文档入仓库、
templates/、third-party.md、AGENTS.md）。

## 丙交付记录（2026-10-24，DEV-20261024-010 / H-003 / PR#3）

| 交付项 | 需求 | 状态 | 证据 |
|---|---|---|---|
| 任务日期测试补充 | R13 | [x] | tst_domain 新增 4 用例（进度越界/前驱缺失/跨月/跨年），15→19 |
| 任务命令 TaskCommands | R13/X03 | [x] | src/commands/TaskCommands.h/.cpp；tst_tasks 5 用例（增删改/关联/解除/清依赖/日期拒绝） |
| 甘特任务表模型 TaskTableModel | R13 | [x] | src/charts/TaskTableModel.h/.cpp；tst_charts 5 用例（5列/字段/格式/里程碑/编辑信号） |
| 规则检查 CheckService | X04 | [x] | src/features/CheckService.h/.cpp；tst_check 7 用例（C01/C02/C06/C07/C08/C09/排序） |
| 复习卡片 ReviewService | X01 | [x] | src/features/ReviewService.h/.cpp；tst_review 6 用例（正反面/空卡/变化/删除/刷新） |
| 自动测试总计 | — | [x] | 8 套件 72 用例全过（Debug）；见 H-003 §13.6 |

仍未实现（丙）：GanttView 时间轴、ReviewPage/问题侧栏 UI、TaskCommands 界面接线、DeepSeek 真实接入。

## 后续阶段门禁（未开始）

- [ ] M1：R01/R04/R06基础、S01关键路径通过
- [ ] M2：R02/R03/R05/R07至R10全部有证据
- [ ] M3：R11至R13实际可编辑、保存、重开
- [ ] M4：四项离线特色断网演示；原图片要求全部可用
- [ ] M5：两项真实联网演示成功；失败不影响原图
- [ ] M6：全量回归、干净Windows安装包验证、依赖许可证清单

## M0期间已发现并修复的缺陷

- ProjectJson.cpp外层namespace未闭合导致嵌套（首轮搭建自查发现，已修复）；
- readFlowNode的position子对象缺少未知字段检查且残留无效const_cast（已修复）；
- viewport/filter子对象补齐unknownKeys检查（已修复）；
- 测试中lambda内QVERIFY与QMap::first顺序依赖（已修复）；
- .gitignore曾全局忽略*.lingya导致fixtures无法入库（统一规范§9.4核对时发现，已按规范修正）。
