# Corvusitor 架构

Corvusitor 负责从仿真器（verilator/gsim 等）产出的模块中发现拓扑、做 corvus 语义分类，并生成可直接运行的 CorvusTop/SimWorker 代码。

## 流水线概览
- 模块发现与解析：`ModuleDiscoveryManager` + `ModuleParser` 识别 comb/seq/external 模块并提取端口信息。
- 连接分析：`ConnectionBuilder::analyze` 将端口分组并分类为 corvus 语义，返回 `ConnectionAnalysis`。
- 目标生成：`CodeGenerator` 封装上述步骤并接受 `mbus_count`/`sbus_count` 配置，将 `ConnectionAnalysis` 交给目标生成器（当前为 `CorvusGenerator`）。
- 产出：`CorvusGenerator` 生成 `<output>_corvus.json`（分析快照）和 `<output>_corvus_gen.h`（CorvusTopModuleGen/CorvusSimWorkerGenP* 实现）。

## 输入约束（来自仿真输出）
- 模块类型：`corvus_comb_P*`、`corvus_seq_P*`、可选 `corvus_external`。comb/seq 数量相同（N），external 最多 1 个。
- 命名/位宽：同名端口必须同位宽，否则非法；输出可 fanout 至多路输入，输入最多被 1 个输出驱动。
- 顶层端口：未被驱动的输入视为 I（top_inputs），无接收的输出视为 O（top_outputs），均属于 comb。
- 约束：`seq_Px` 的输入仅可来自同分区 `comb_Px`；Ei=external 输入只能由 comb 驱动，Eo=external 输出只能驱动 comb 输入。

## 数据模型（ConnectionAnalysis）
- 顶层：`top_inputs` / `top_outputs`；缺 driver/receiver 分别视为 I/O。
- External：`external_inputs`（comb→external，Ei） / `external_outputs`（external→comb，Eo）。
- 分区：`partitions[pid]`，包含本地 `local_cts_to_si`、`local_stc_to_ci`，以及跨分区 `remote_s_to_c`。
- 每条连接携带 `port_name`、`width`/`width_type`（对应 `PortWidthType` 枚举）、`driver`、`receivers`（module/port 名及指针）。

### 分类规则概要
1. 按端口名聚合并比对位宽，得到原始 `PortConnection`（支持多 driver/receiver）。
2. 仅为“将要接收”的信号建立 slot，未被消费的 driver 不膨胀计数。
3. 按 receiver 拆分分类：
   - 无 driver → `top_inputs`
   - 无 receiver → `top_outputs`
   - COMB driver：同分区 SEQ → `local_cts_to_si`；EXTERNAL → `external_inputs`；其他类型告警
   - SEQ driver：COMB 同分区 → `local_stc_to_ci`；COMB 跨分区 → `remote_s_to_c`；其他类型告警
   - EXTERNAL driver：COMB → `external_outputs`；其他类型告警

## 总线与 slot 模型
- MBus：Top↔Worker。Top 下发 I/Eo，Worker 上送 O/Ei，`targetId`=0 表示 Top，1..N 表示各分区。
- SBus：Worker↔Worker。仅承载 `remote_s_to_c`（S→C 跨分区）。
- 本地直连：同分区的 Ct→Si / St→Ci 直接内存拷贝，不经总线。
- Slot 分配：按接收端独立规划 slot 空间，slotBits 取 {8,16,32} 中满足 slot 数的最小值（ceil(log2(slotCount)) 后向上取整）。
- Chunk 规划：48-bit payload 预算下优先选择 dataBits=32/16/8；若宽度超出则启用 chunkBits=8/16/32 分片，`chunkCount`=ceil(width/dataBits)，必要时提升 chunkBits 直至覆盖。
- 编码布局：`chunkIdx | data | slotId`，slotId 永远在最低位；未分片的信号 chunkBits=0、chunkIdx 视作 0。
- 辅助库：`boilerplate/corvus/corvus_codegen_utils.h` 提供位掩码、payload 打包/解包，以及 VlWide 的跨 word 读写。

## 生成代码结构
- `CorvusTopModuleGen`：持有 `TopPortsGen`，负责 I/Eo 发送与 O/Ei 接收，并断言 `kCorvusGenMBusCount`。
- `CorvusSimWorkerGenP*`：负责 comb/seq 实例化、MBus/SBus 收发、远端 S→C 解码、本地 Ct→Si / St→Ci 直连，并断言 `kCorvusGenMBusCount`/`kCorvusGenSBusCount`。
- 生成头文件依赖通用 boilerplate（module_handle/top_ports/corvus_sim_worker 等），可直接纳入上层工程编译。

## Boilerplate 基线（CModel）
- 总线（`boilerplate/corvus_cmodel/corvus_cmodel_idealized_bus.{h,cpp}`）：`CorvusCModelIdealizedBus` 管理 `vector<std::shared_ptr<CorvusCModelIdealizedBusEndpoint>>`；Endpoint 内部 `deque<uint64_t>` 作为收发缓冲，`send` 在 bus 协助下写入目标端点（写路径加锁，读不加锁），`recv` 空时返回 0，支持 `bufferCnt` / `clearBuffer` 查询与清理。Bus 构造时固定端点数，提供 `getEndpointCount`/`getEndpoint(s)`。
- 同步树（`boilerplate/corvus_cmodel/corvus_cmodel_sync_tree.{h,cpp}`）：`CorvusCModelSyncTree` 生成 master + N worker 端点（shared_ptr 管理），维护 `masterSyncFlag`、`simCoreCFinishFlag[]`、`simCoreSFinishFlag[]`。Master 端点的 `isMBusClear`/`isSBusClear` 恒为 true；`getSimCore*FinishFlag` 仅在全一致时返回，否则 PENDING。Worker 端点可设 C/S finish flag，能读取 masterSyncFlag。
- SimWorker 骨架（`boilerplate/corvus/corvus_sim_worker.{h,cpp}`）：构造传入 synctree 端点与 m/sBus 端点指针；虚方法需由生成器实现：`createSimModules`/`deleteSimModules`、`loadBusCInputs`、`sendCOutputsToBus`、`loadSInputs`、`sendSOutputs`、`loadLocalCInputs`，并通过 `init`/`cleanup` 触发生命期管理。
- Top 模块骨架（`boilerplate/corvus/corvus_top_module.{h,cpp}`）：构造传入 top synctree 端点与 mBus 端点指针；生成器需实现 `createExternalModule`/`deleteExternalModule`、`sendIAndEOutput`、`loadOAndEInput`、`resetSimWorker`，`clearMBusRecvBuffer` 已默认清空接收缓存。
- Worker 线程运行器（`boilerplate/corvus_cmodel/corvus_cmodel_sim_worker_runner.{h,cpp}`）：`CorvusCModelSimWorkerRunner` 接收 `vector<std::shared_ptr<CorvusSimWorker>>`，`run` 为每个 worker 启动线程执行 `loop`，`stop` 停止并回收线程。
