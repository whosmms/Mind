# PR44甲独立复核证据

2026-10-31，被测丙tip`9b302ea89e94468570393e83b3316f5b9fb7f181`（产品0573d77）。主线d770c93文档随后普通同步为28afca8，产品与测试保持原tip。

- debug/release各24份XML：各24套件528通过、0失败0跳过；UI97、甘特30、节点命令63。构建/部署/CTest输出同目录，CTest各24/24。validation.json保留被测SHA和逐套退出码。
- pr44-negative-zero.xml：起点22→0，穿字几何断言实红，2通过/1失败。
- pr44-negative-axis.xml：起点22→34，轴带可见断言实红，2通过/1失败。
- pr44-negative-results.json：负对照的源断言行和消息。
- pr44-restored.xml：源码字节/哈希精确恢复后甘特全套30通过，0失败0跳过。
- pr44-capture.xml与pr44-gantt-host.png：宿主拖动/选择专项3通过，0失败0跳过，截图已实际查看；不与528相加，不代签真人。
- pr44-format-diff.txt：新增测试行格式差异，97行输出，不是97处代码错误，未擅自替维护人重排。

复验先运行冻结环境脚本，TEMP/TMP设为工程内build/environment/temp，QT_QPA_PLATFORM=offscreen，QT_PLUGIN_PATH指向build/debug绝对路径。分别对debug/release执行配置、构建、准备运行依赖与CTest；逐套tst_*.exe输出XML核对。原始脚本build/验证PR44原提交.ps1、build/验证PR44双反证.ps1保留本机，后者以finally恢复原始字节并核对SHA-256，只改本轮自有评审工作区。

完整结论见docs/reviews/PR44-甲非作者复核.md。甲接收当前显示与运行，不代签乙规则、丙自测或真人/输入法/DPI/无Qt跨机，不宣告M3/M4通过。
