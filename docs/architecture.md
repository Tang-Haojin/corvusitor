# Corvusitor 架构

Corvusitor 负责从仿真器产出的 Verilator 模块中发现拓扑、做 corvus 语义分类，并生成可直接运行的 CorvusTop/SimWorker/CModel 封装代码（VCS/Modelsim 解析仍为占位，发现会报错）。

## 流水线概览
- 模块发现：`ModuleDiscoveryManager` 并行尝试 Verilator/VCS/Modelsim 目录模式，当前只有 Verilator 模式可正常解析（`verilator-compile-<mod>/V<mod>.h`），命中未实现的解析器会抛错。
- 模块解析：`ModuleParserFactory` 创建对应 parser，Verilator 通过正则解析 `VL_IN/OUT(8|16|64|W)` 宏获取端口方向/位宽；生成 `ModuleInfo`（包含实例名/分区/端口列表）。
- 拓扑校验：`CodeGenerator::load_data` 校验 comb/seq 数量一致且均 >0，external ≤1，随后用 `ConnectionBuilder::analyze` 做 corvus 分类（出现约束违例直接抛出 runtime_error）。
- 目标生成：`CodeGenerator` 根据 CLI 选择 `CorvusGenerator` 或 `CorvusCModelGenerator`，并传入 `mbus_count`/`sbus_count`。`CorvusGenerator` 负责 JSON + Top/Worker 代码；CModel 目标在此基础上追加模拟器封装。
- 输出约定：类名前缀源自 `--output-name`（先做路径基名 + 非字母数字替换 + 前缀 `C`），文件前缀为 `<output_dir>/<output_name>`，因此输出路径与类名互不混用。

## 输入约束（来自仿真输出）
- 模块形态：`corvus_comb_P*`、`corvus_seq_P*`、可选 `corvus_external`；comb/seq 数量必须相同且 >0，external 最多 1（在 `load_data` 里强校验）。
- 目录命名：Verilator 期望 `verilator-compile-<module>` 目录下有 `V<module>.h`；发现 VCS/Modelsim 模式会报 “parser not implemented”。
- 端口命名/位宽：同名端口必须同位宽同类型，宽度不一致或多 driver 会直接抛错；VL_W 宽度按端点的 `array_size` 处理。
- 允许的驱动关系：
  - 无 driver → 视为顶层输入（所有接收端必须是 COMB，否则报错）。
  - COMB driver 只能驱动同分区 SEQ 或 external；跨分区 SEQ 会报错。
  - SEQ driver 只能驱动 COMB，同分区记作本地，跨分区记作 remote S→C。
  - EXTERNAL driver 只能驱动 COMB。
  - COMB 驱动未被消费时生成顶层输出（SEQ/EXTERNAL 未被消费的输出直接忽略）。

## 数据模型（ConnectionAnalysis）
- 顶层：`top_inputs` / `top_outputs`。
- External：`external_inputs`（comb→external，Ei） / `external_outputs`（external→comb，Eo）。
- 分区：`partitions[pid]`，包含本地 `local_c_to_s`、`local_s_to_c`，以及跨分区 `remote_s_to_c`。
- 每条连接携带 `port_name`、`width`/`width_type`（对应 `PortWidthType` 枚举）、`driver`、`receivers`（module/port 指针）。

### 分类规则概要
1. 按端口名聚合，强制单 driver、位宽一致。
2. 无 driver → 记录到 `top_inputs`；无 receiver 且 driver 为 COMB → 记录到 `top_outputs`。
3. 其余按 driver/receiver 逐一拆分：
   - COMB → 同分区 SEQ：`local_c_to_s`；→ EXTERNAL：`external_inputs`；其他接收类型报错。
   - SEQ → COMB：同分区 `local_s_to_c`，跨分区 `remote_s_to_c`；其他类型报错。
   - EXTERNAL → COMB：`external_outputs`；其他类型报错。
4. 以上任何违例直接抛出异常，不以 warning 继续（`analysis.warnings` 当前未使用）。

## 总线与 Slot 规划
- Slot 粒度：固定 16-bit，宽信号按 16-bit 片递增 slotId（低位在低 slotId），Top 与各 Worker 的 slot 空间独立。
- targetId 语义：Top=0，分区 pid 的 Worker=pid+1；MBus 承载 Top↔Worker 的 I/O，SBus 仅承载 `remote_s_to_c`。
- Worker 侧 slot 编址：`next_slot` 自增复用在 MBus 拉取与 SBus 拉取（针对同一 Worker 的 C 输入），本地 copy 不占 slot。
- Top 侧 slot 编址：顶层输出与 external 输入共用一张 slot 表（独立于任一 Worker）。
- 计划排序：在写 JSON 前对 send/recv/copy 记录排序，保证 determinism。

## 生成代码结构
- `C<output>TopModuleGen`：派生自 `CorvusTopModule`，内含 `TopPortsGen`（自动生成顶层 I/O 字段）；在构造时 `assert` MBus 端点数量。`sendIAndEOutput` 按编译期硬编码的 slotId/targetId 从 `TopPortsGen`/external 读取，轮询 mBus 端点发送；`loadOAndEInput` 逐端点 `bufferCnt` 全部读空，switch-case 直写 `TopPortsGen`/external，VL_W 通过 word+bit 偏移写回。
- `C<output>SimWorkerGenP*`：派生自 `CorvusSimWorker`，构造时校验 MBus/SBus 端点数；`createSimModules`/`deleteSimModules` 用 `VerilatorModuleHandle` 管理 comb/seq。输入阶段分别将 MBus/SBus 缓冲读空并按 slotId 解码到 comb 端口；输出阶段 round-robin 发送到目标端点（C 输出 targetId=0，S 输出 targetId=分区+1）；`copySInputs`/`copyLocalCInputs` 直接做成员赋值（VL_W 做逐 word 拷贝）。
- 产物：`<output>_connection_analysis.json`、`<output>_corvus_bus_plan.json`、`C<output>TopModuleGen.{h,cpp}`、`C<output>SimWorkerGenP<ID>.{h,cpp}`、聚合头 `C<output>CorvusGen.h`。

## Boilerplate 基线（CModel）
- 总线：`corvus_cmodel_idealized_bus` 提供固定端点数的 FIFO 总线，`send` 写入目标端点（写路径加锁，读不加锁），`recv` 空时返回 0；支持 `bufferCnt`/`clearBuffer`。
- 同步树：`corvus_cmodel_sync_tree` 生成 Top/Worker 端点，Top 的 `isMBusClear`/`isSBusClear` 永远为 true，Worker 端点上报 `simWorkerSync` 等旗标。
- Worker 线程：`corvus_cmodel_sim_worker_runner` 为每个 Worker 开线程跑 `loop()`，`stop` 负责回收。
- CModel 生成：`C<output>CModelGen` 在 `CorvusCModelGenerator` 中生成，固定 `worker_count`=分区数量，`endpoint_count`=maxPid+2；构造时创建总线/同步树、Top 与所有 Worker，并立即启动线程。公开 `eval()`（依次调用 Top::eval + Top::evalE）、`stop()`、`ports()`/`workers()` 访问器。

## 同步机制（当前实现）
- ValueFlag 语义：8-bit 环形计数，0 代表 PENDING，`nextValue()` 跳过 0。
- Top 周期（`CorvusTopModule::eval`）：
  1) `sendIAndEOutput()` 下发本轮 I/Eo；
  2) 等待 `isMBusClear()` 与 `isSBusClear()`；  
  3) `raiseTopSyncFlag()` 更新同步旗标；  
  4) 轮询 `isSimWorkerInputReadyFlagRaised()`（仅基于旗标，不检查总线）；  
  5) `raiseTopAllowSOutputFlag()`；  
  6) 等待 `isMBusClear()` 且 `isSimWorkerSyncFlagRaised()`；  
  7) `loadOAndEInput()` 读取 O/Ei。
  启动前可调用 `prepareSimWorker()` 设置 `START_GUARD`。
- Worker 周期（`CorvusSimWorker::loop`）：
  1) 启动守卫：自旋直到看到 `START_GUARD`；  
  2) 轮询 `isTopSyncFlagRaised()` 进入本轮；  
  3) `loadMBusCInputs()` → `loadSBusCInputs()` → `raiseSimWorkerInputReadyFlag()`；  
  4) `cModule->eval()` → `sendMBusCOutputs()` → `copySInputs()`；  
  5) `sModule->eval()` → 等待 `isTopAllowSOutputFlagRaised()` → `sendSBusSOutputs()`；  
  6) `raiseSimWorkerSyncFlag()` → `copyLocalCInputs()`；  
  7) 依 `loopContinue` 决定下一轮。
- 一致性与错误处理：Worker 若观测到 topSync/topAllow 跳变至非期望值会陷入 fatal 循环；Top 若观测到 simWorkerSync 跳变到非 nextValue() 也会持续报错。Top 仅在同步前后等待 MBus/SBus 清空，Worker 不主动清空接收缓冲。
