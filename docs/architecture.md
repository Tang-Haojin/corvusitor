# Corvusitor 架构

 Corvusitor 负责从仿真器（当前仅实现 Verilator，其余暂未实现）产出的模块中发现拓扑、做 corvus 语义分类，并生成可直接运行的 CorvusTop/SimWorker 代码。

## 流水线概览
- 模块发现与解析：`ModuleDiscoveryManager` + `ModuleParser` 识别 comb/seq/external 模块并提取端口信息（VCS/Modelsim 解析器为占位，尚未实现）。
- 连接分析：`ConnectionBuilder::analyze` 将端口分组并分类为 corvus 语义，返回 `ConnectionAnalysis`。
- 目标生成：`CodeGenerator` 封装上述步骤并接受 `mbus_count`/`sbus_count` 配置，将 `ConnectionAnalysis` 交给目标生成器（当前为 `CorvusGenerator`）。
- 产出：`CorvusGenerator` 生成 `<output>_corvus.json`（分析快照），以及与类名一致的生成文件：`C<output>TopModuleGen.{h,cpp}`/`C<output>SimWorkerGenP*.{h,cpp}`（聚合头 `C<output>CorvusGen.h`），类名前缀携带用户 output；CModel 入口由 `CorvusCModelGenerator` 生成 `C<output>CModelGen.h`。

## 输入约束（来自仿真输出）
- 模块类型：`corvus_comb_P*`、`corvus_seq_P*`、可选 `corvus_external`。comb/seq 数量相同（N），external 最多 1 个（已在代码中校验）。
- 命名/位宽：同名端口必须同位宽，否则非法；输出可 fanout 至多路输入，输入最多被 1 个输出驱动（多 driver 会被拒绝）。
- 顶层端口：未被驱动的输入视为 I（top_inputs），无接收的输出视为 O（top_outputs），均属于 comb。
- 约束：`seq_Px` 的输入仅可来自同分区 `comb_Px`；Ei=external 输入只能由 comb 驱动，Eo=external 输出只能驱动 comb 输入。

## 数据模型（ConnectionAnalysis）
- 顶层：`top_inputs` / `top_outputs`；缺 driver/receiver 分别视为 I/O。
- External：`external_inputs`（comb→external，Ei） / `external_outputs`（external→comb，Eo）。
- 分区：`partitions[pid]`，包含本地 `local_c_to_s`、`local_s_to_c`，以及跨分区 `remote_s_to_c`。
- 每条连接携带 `port_name`、`width`/`width_type`（对应 `PortWidthType` 枚举）、`driver`、`receivers`（module/port 名及指针）。

### 分类规则概要
1. 按端口名聚合并比对位宽，得到原始 `PortConnection`（支持多 driver/receiver）。
2. 仅统计“将要接收”的信号，未被消费的 driver 不膨胀计数。
3. 按 receiver 拆分分类：
   - 无 driver → `top_inputs`
   - 无 receiver → `top_outputs`
   - COMB driver：同分区 SEQ → `local_c_to_s`；EXTERNAL → `external_inputs`；其他类型告警
   - SEQ driver：COMB 同分区 → `local_s_to_c`；COMB 跨分区 → `remote_s_to_c`；其他类型告警
   - EXTERNAL driver：COMB → `external_outputs`；其他类型告警

## 总线模型
- MBus：Top↔Worker。Top 下发 I/Eo，Worker 上送 O/Ei，`targetId`=0 表示 Top，1..N 表示各分区。
- SBus：Worker↔Worker。仅承载 `remote_s_to_c`（S→C 跨分区）。
- 本地直连：同分区的 Ct→Si / St→Ci 直接内存拷贝，不经总线。
- 辅助库：`boilerplate/corvus/corvus_helper.h` 提供位掩码、payload 打包/解包，以及 VlWide 的跨 word 读写。

## 生成代码结构
- `C<output>TopModuleGen`：持有 `TopPortsGen`，负责 I/Eo 发送与 O/Ei 接收，并在运行期 `assert` 校验 `kCorvusGenMBusCount`。
- `C<output>SimWorkerGenP*`：负责 comb/seq 实例化、MBus/SBus 收发、远端 S→C 解码、本地 Ct→Si / St→Ci 直连，并在运行期 `assert` 校验 `kCorvusGenMBusCount`/`kCorvusGenSBusCount`。
- 生成头文件依赖通用 boilerplate（module_handle/top_ports/corvus_sim_worker 等），可直接纳入上层工程编译。

## Boilerplate 基线（CModel）
- 总线（`boilerplate/corvus_cmodel/corvus_cmodel_idealized_bus.{h,cpp}`）：`CorvusCModelIdealizedBus` 管理 `vector<std::shared_ptr<CorvusCModelIdealizedBusEndpoint>>`；Endpoint 内部 `deque<uint64_t>` 作为收发缓冲，`send` 在 bus 协助下写入目标端点（写路径加锁，读不加锁），`recv` 空时返回 0，支持 `bufferCnt` / `clearBuffer` 查询与清理。Bus 构造时固定端点数，提供 `getEndpointCount`/`getEndpoint(s)`。
 - 同步树（`boilerplate/corvus_cmodel/corvus_cmodel_sync_tree.{h,cpp}`）：`CorvusCModelSyncTree` 生成 TopModule + N SimWorker 端点（shared_ptr 管理），维护 `topSyncFlag`、`simWorkerSFinishFlag[]`。Top 端点的 `isMBusClear`/`isSBusClear` 恒为 true；`getSimWorkerSFinishFlag` 仅在全一致时返回，否则 PENDING。SimWorker 端点可设 S finish flag，能读取 `topSyncFlag`。
- SimWorker 骨架（`boilerplate/corvus/corvus_sim_worker.{h,cpp}`）：构造传入 synctree 端点与 m/sBus 端点指针；虚方法需由生成器实现：`createSimModules`/`deleteSimModules`、`loadRemoteCInputs`、`sendRemoteCOutputs`、`loadSInputs`、`sendRemoteSOutputs`、`loadLocalCInputs`，并通过 `init`/`cleanup` 触发生命期管理。
- Top 模块骨架（`boilerplate/corvus/corvus_top_module.{h,cpp}`）：构造传入 top synctree 端点与 mBus 端点指针；生成器需实现 `createExternalModule`/`deleteExternalModule`、`sendIAndEOutput`、`loadOAndEInput`、`resetSimWorker`，`clearMBusRecvBuffer` 已默认清空接收缓存。
- Worker 线程运行器（`boilerplate/corvus_cmodel/corvus_cmodel_sim_worker_runner.{h,cpp}`）：`CorvusCModelSimWorkerRunner` 接收 `vector<std::shared_ptr<CorvusSimWorker>>`，`run` 为每个 worker 启动线程执行 `loop`，`stop` 停止并回收线程。

## 同步机制（当前实现）
- 顶层/Worker 角色与端点
   - 顶层 `TopModule`：持有 [boilerplate/corvus/corvus_top_module.h](boilerplate/corvus/corvus_top_module.h) / [boilerplate/corvus/corvus_top_module.cpp](boilerplate/corvus/corvus_top_module.cpp)。通过 `CorvusTopSynctreeEndpoint` 管理 Top 同步与全局完成标志；通过 `mBusEndpoints` 与各分区通信。
   - Worker `SimWorker`：持有 [boilerplate/corvus/corvus_sim_worker.h](boilerplate/corvus/corvus_sim_worker.h) / [boilerplate/corvus/corvus_sim_worker.cpp](boilerplate/corvus/corvus_sim_worker.cpp)。通过 `CorvusSimWorkerSynctreeEndpoint` 观察 Top 同步与上报 S 阶段完成；持有 `mBusEndpoints`/`sBusEndpoints`。
   - 同步树接口：见 [boilerplate/corvus/corvus_synctree_endpoint.h](boilerplate/corvus/corvus_synctree_endpoint.h)。`ValueFlag` 为 8-bit 环形计数（保留 0 作为 PENDING，`nextValue()` 永不返回 0）。

- 顶层周期（`TopModule::eval()`）
   - 发送 Top 输入与 External 输出：`sendIAndEOutput()`。
   - 等待总线清空：同时等待 `isMBusClear()` 和 `isSBusClear()` 为真（确保上一轮所有帧已被消费）。
   - 提升 Top 同步标志：`topSyncFlag.updateToNext()` 后写入 `setTopSyncFlag(topSyncFlag)`。
   - 等待 Worker 报告 S 阶段完成：先等待 MBus 清空，再等待 `getSimWorkerSFinishFlag()` 从 `prevSFinishFlag.nextValue()` 达到一致；成功后将 `prevSFinishFlag.updateToNext()`。
   - 接收 Top 输出与 External 输入：`loadOAndEInput()`。
   - 额外：`prepareSimWorker()` 在启动前设置 `setSimWorkerStartFlag(START_GUARD)`；`evalE()` 驱动 external 的 `eHandle->eval()`。

- Worker 周期（`SimWorker::loop()`）
   - 等待 Top 同步变化：轮询 `getTopSyncFlag()`，当等于本地 `prevTopSyncFlag.nextValue()` 时，更新 `prevTopSyncFlag.updateToNext()` 并进入本轮。
   - C 阶段：`loadRemoteCInputs()` → `cModule->eval()` → `sendRemoteCOutputs()`。
   - S 阶段：`loadSInputs()` → `sModule->eval()` → `sendRemoteSOutputs()`。
   - 上报完成：`raiseSFinishFlag()` 将本地 `sFinishFlag.updateToNext()` 并调用 `setSFinishFlag(sFinishFlag)`；随后执行 `loadLocalCInputs()` 做本地 St→Ci 的直连拷贝。
   - 停止：`stop()` 置位 `loopContinue=false`，循环在下一次检查时退出。

- 一致性与错误处理
   - 顶层对 S 完成的判断遵循“全 worker 一致”的语义，`getSimWorkerSFinishFlag()` 未达成一致时返回 `0`（PENDING）。
   - Worker 在 `isTopSyncFlagRaised()` 中若看到非期望值（既不是当前 `prevTopSyncFlag`，也不是 `nextValue()`）将视为严重错误并调用 `stop()`。
   - 顶层在 `allSimWorkerSFinish()` 中若发现跳变（从当前直接跳到非 `nextValue()`）会输出致命错误并 `exit(1)`。

- 总线缓冲约束
   - 顶层在提升 Top 同步前后都会等待总线清空，避免跨轮残留。
   - Worker 端按阶段耗尽缓冲：输入阶段拉取所有待处理帧，输出阶段尽快发送；结束后不主动清空接收缓冲（由接收者负责）。
