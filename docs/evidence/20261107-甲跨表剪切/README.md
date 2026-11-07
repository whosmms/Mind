# H-037跨表剪切证据（甲，2026-11-07）

被测产品`2e5070275797162fa46882a2c5ed6e53faa3f728`，基线main308a7ce。冻结Qt6.8.3/MinGW13.1.0/CMake3.30.5/Ninja1.12.1/clang-format19.1.7。

| 项目 | Debug | Release |
|---|---|---|
| 构建/部署/CTest | 成功，24/24 | 成功，24/24 |
| 全量XML | 24套件538通过、0失败0跳过 | 24套件538通过、0失败0跳过 |
| UI/节点命令/甘特 | 101/69/30 | 101/69/30 |

48份XML与构建/部署/CTest文本在debug/release，validation.json保留被测SHA、逐套退出码及统计。本轮HEAD冻结；UI四个数据行、命令两个数据行新增，其余乙丙成果不归甲。旧测试的多字符/溢出与QDateTime弃用警告在构建日志中保留，没有声称零告警。

- h037-ui-initial.xml：原跨表命令消费到鱼骨目标，迁移根order实际0/期望6，8通过1失败，含init/cleanup。
- h037-command-red.xml：原命令追加/插入两行分别实际0/期望2、0/期望1，2通过2失败，含init/cleanup。
- h037-command-targeted.xml：最小补丁后命令专项8通过；h037-ui-targeted.xml：UI专项9通过，均0失败0跳过，另计。
- h037-candidate.xml：基础样例导出与跨表UI四行共7通过，0失败0跳过，另计；6基础样例由既有tst_ui导出。
- H037导出跨表样例.cpp：独立临时工具，复用工厂与FileService创建另4组受控样例，保存/严格重开/JSON相等；--verify-existing对全部10样例严格加载，退出0才归档。源文件供重建，未把本机工程/账号数据提交。
- 候选程序校验.json：`D:/mind/人工验收/跨表剪切_20261107`，19文件含qt.conf逐项SHA-256、10样例哈希；精简PATH原生Windows平台8秒存活，实际加载路径在本包，主动停止的只是本轮自己启动的进程。原插件日志在候选启动日志，启动插件加载日志.txt为按本机GBK正确解码的UTF8副本。
- 日常主线验证.json/日常主线程序校验.json另对应308a7ce：两配置24/532/0/0，44份本机运行库/插件库存（含可选Svg和测试平台插件，不是最小分发包），实际8秒启动与路径核对。原始48XML在app/build/main-20261107-sync-evidence。
- PR44-甲补登回执_20261107.json：用户本轮授权后发布原10月31日真实复核的回执，不把旧528写成本轮重跑。

复验时先启用tools/进入开发环境.ps1，把TEMP/TMP指向工程内build/environment/temp，QT_QPA_PLATFORM=offscreen，QT_PLUGIN_PATH指向对应build/debug或release绝对目录。对两配置各执行cmake --preset、cmake --build --preset、准备运行依赖.ps1与ctest --preset；全量逐套tst_*.exe导出XML核对。

原始驱动build/验证H037跨表剪切.ps1、build/归档H037跨表剪切.ps1、build/h037-cross-sheet-evidence及console日志保留本机。临时样例工具用g++ -std=c++17、src和QtCore头目录，链接build/release的lingya_appcore/lingya_core及Qt6Widgets/Gui/Network/Core/advapi32编译；依赖版本与产品相同。

尚待乙对共享命令消费者补丁的维护人复核、丙用户流程接收。路线/文稿后续策略、CARD_SHEET规则待议、IF-005/真人/阶段门禁不代签。
