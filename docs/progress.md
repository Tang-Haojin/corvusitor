# 阶段性进展（当前）

- 解析与校验：`ModuleDiscoveryManager` 支持混合目录探测，当前仅 Verilator 模式落地（命中 VCS/Modelsim 解析会报错）；`CodeGenerator::load_data` 校验 comb/seq 数量一致且 >0、external ≤1，同名端口单 driver、位宽一致，否则直接抛错。
- 连接分类：`ConnectionBuilder::analyze` 按端口名聚合并拆分 driver/receiver；无 driver 归类顶层输入，COMB 无 receiver 产生顶层输出；COMB→同分区 SEQ、本地 Ct→Si；SEQ→COMB（本地或远端 S→C）；EXTERNAL→COMB；任何非法组合直接报错（`warnings` 当前未使用）。
- 生成产物：`CorvusGenerator` 输出 `<output>_connection_analysis.json`、`<output>_corvus_bus_plan.json`（send/recv/copy 已排序以保证 determinism），并生成 `C<output>TopModuleGen` / `C<output>SimWorkerGenP<ID>` / 聚合头 `C<output>CorvusGen.h`。Slot 固定 16-bit 片、Top/Worker 独立编号，发送端 round-robin 选择总线端点，构造时断言 MBus/SBus 端点数。
- CModel：`CorvusCModelGenerator` 在上述基础上生成 `C<output>CModelGen`，使用 idealized bus + synctree（endpoint_count=maxPid+2），构造时创建并启动全部 worker 线程（`CorvusCModelSimWorkerRunner`），暴露 `eval()` / `stop()` / `ports()` / `workers()`。
- 运行时时序：Top 流程为 sendIAndEOutput → 等待 MBus/SBus 清空 → raiseTopSyncFlag → 等待 simWorkerInputReadyFlag → raiseTopAllowSOutputFlag → 等待 MBus 清空且 simWorkerSyncFlag → loadOAndEInput；Worker 流程为等待 START_GUARD → 等待 topSyncFlag → 拉取 M/SBus 输入并上报 ready → C eval + MBus 输出 + Ct→Si 拷贝 → S eval + 等待 topAllowSOutput → SBus 输出 → 上报 sync + St→Ci 拷贝。旗标跳变到非预期值时会持续打印 fatal。
- 路由策略：targetId 固定（Top=0，Worker pid=pid+1）；MBus 承载 I/Eo 下行与 O/Ei 上行，SBus 仅承载跨分区 S→C，本地 Ct→Si / St→Ci 通过 memcpy 直连；帧格式 48-bit（高 16-bit 为数据、低 32-bit 为 slotId）。
- CLI 与输出：支持 `--modules-dir` / `--module-build-dir`（后者优先）、`--mbus-count` / `--sbus-count`（最小 1）、`--target corvus|cmodel`；`--output-dir` + `--output-name` 拼成输出前缀，同时对 output-name 做字符清洗后生成类名前缀 `C<token>`。
- 测试覆盖：`make test_corvus_gen`、`make test_corvus_slots`、`make test_corvus_yuquan`、`make test_corvus_yuquan_cmodel`（YuQuan 测试需先在 `test/YuQuan` 下生成 Verilator 工件）。

详见 `docs/architecture.md`（架构/约束/生成）与 `docs/workflow.md`（使用与运行时时序）。
