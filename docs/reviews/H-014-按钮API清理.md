# H-014：P3按钮文字API独立清理

交付人：甲takagi。关联DEV-20261025-007，来自乙对PR #17的非阻断意见。
分支：fix/a/M1-button-api。基线：PR #17的3a090ce（源码与乙已评85f0a1b一致）。
本轮在独立Git工作区D:/mind/.worktrees/button-api实现；原D:/mind/app分支和待验程序保留。

## 修改及依据

本机Qt6.8.3的QtWidgets/qmessagebox.h把buttonText和setButtonText标为Qt6.2起弃用，建议通过button()取得按钮后使用text()/setText()。

- MainWindow.cpp：四处setButtonText改为button()->setText()，分别为确定、保存、放弃修改、取消。
- tst_ui.cpp：三处buttonText改为button()->text()，仍验证同样的中文标签和取消保留内容。
- 不改变标准按钮集合、默认/取消按钮、中文文字、错误类型、保存/放弃/取消分支或命令事务。
- 不新增镜像测试，沿用已有提示中文、取消新建/关闭、保存、恢复等集成测试。
- 不修改NodeCommands.cpp或乙已认领的tst_nodecommands直接order断言；不做节点二次测量性能重构。

## 验证

被测代码4dba123，冻结工具：Qt6.8.3、MinGW13.1.0、CMake3.30.5、Ninja1.12.1、clang-format19.1.7。

```powershell
.\tools\验证环境.ps1 -Configuration Both -ToolsRoot D:\mind\.tools
git diff --check
```

4dba123上实测：原验证脚本退出0，Debug/Release各15套件246项通过、0失败0跳过；tst_ui各32项，含QtTest初始化/清理。
新工作区完整构建日志没有deprecated-declarations或is deprecated诊断；src/tests已不再调用buttonText/setButtonText。
逐套30份XML在build/p3-evidence/debug及release，原构建报告保存在build/p3-evidence/validation.json和p3-validation-console.log（保留独立快照，避免后续预演覆盖）。
共享[脱敏摘要](../evidence/20261025-甲P3按钮API/自动验证摘要.json)不含电脑名、凭据或个人工具安装目录。
本轮没有界面行为或样式变化，不增加真人验收清单，不将旧人工结果扩展到未经实际操作的新行为。

## 评审与集成顺序

作为PR #17后续的独立P3草稿增量审阅，不提前合入待验分支。待PR #17按原约定完成丙评审和甲本人补验、由乙集成后，再把本增量同步/改目标到main并按正常规则复核合并。
本轮不改PR #17源码，不自行关闭M1门禁，也不重复催促已知真人待办。

远程交付：[草稿PR #18](https://github.com/whosmms/Mind/pull/18)，目标暂为feature/a/M1-editor-integration，仅展示P3增量。当前未合并；原待验分支仅补充交付/合流说明，原src/tests/CMake及两配置程序哈希均核对未变。

## 当前主线合流准备

2026-10-25只读git merge-tree核对PR17=3a090ce与main=34c8475：当前冲突仅docs/开发日志.md，业务源码自动合并，无分支、索引或工作区修改。
自动合并不等同合流后测试通过；原15套件246项结果只覆盖M1待验基线，不覆盖R10合流。合流后由乙真实构建复验并登记。

建议乙在合并前保存双方日志，逐块保留甲H-011/H-012/H-013记录、用户转交复核回执及乙R10/D-M2-01记录，避免整份取ours/theirs；完成真实合流后再检查布局与画布协作。
该冲突分析仅对应上述两个SHA，后续任一端更新都要重新核对。
