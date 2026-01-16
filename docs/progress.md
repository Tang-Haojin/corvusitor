# 阶段性进展（当前）

- 生成链路：`CodeGenerator` 支持传入 `mbus_count`/`sbus_count`，并持久化模块列表供 `ConnectionBuilder` 使用；CLI 已新增 `--target` 选择 corvus/cmodel。
- 工件输出：`CorvusGenerator` 产出 JSON + 头文件（`C<output>TopModuleGen`/`C<output>SimWorkerGenP*`/`C<output>CorvusGen.h`，类名驼峰且含 output 前缀），MBus/SBus 端点数使用运行期 `assert` 校验。`CorvusCModelGenerator` 已落地，生成 `C<output>CModelGen.h` 并包装 top/worker 入口。
- 路由策略：以接收端需求驱动的路由规划，远端 S→C 路由按 targetId=分区+1。
- 约束校验：连接发现/分类时遇到多 driver、位宽不匹配、非法跨分区或 external 连接会直接抛错终止（不再仅打印 warning）。
- 测试覆盖：`test_corvus_generator`（JSON/头文件烟测）、`test_corvus_slots`（跨分区 S→C）、`test_corvus_yuquan`（YuQuan 集成路径，需先生成 verilator 工件）、`test_corvus_yuquan_cmodel`（YuQuan CModel 集成）。
- 数据结构精简：生成独立的 `connection_analysis` 与 `corvus_bus_plan` JSON；`CorvusBusPlan` 采用 SlotRecvRecord/SlotSendRecord/CopyRecord 记录 slot 编址与拷贝计划（16-bit 对齐，单表覆盖 MBus/SBus，Top 侧共享一张表），不重复落位宽等冗余信息。

来自旧文档的补充
- 输入/约束与分类规则已经汇总到 `docs/architecture.md`。
- 收发时序细节已汇总到 `docs/workflow.md`。
- CLI/输出规则更新：拆分 `--output-dir` 和 `--output-name`，便于跨平台/路径管理；生成文件采用类名驼峰命名，避免与目录拼接混用下划线。

# Corvus 仿真数据同步方案

- 端点寻址：`targetId` 固定、独立于 payload，Top 恒为 0，分区 P 的 SimWorker 恒为 P+1。单帧发送的 targetId 固定，但 Top 可面向所有 worker 逐一发送，Worker 也可选择 Top 或任意其他 worker 作为目标，接收端按自身 slot 编址表解释 payload。
- 帧格式（48-bit 固定宽）：`[47:32]=slotData(16-bit)|[31:0]=slotId(32-bit)`，无额外 header。`slotId` 负责定位接收端内的变量切片，`slotData` 携带 16-bit 原始数据；大小端均按数值位序解释。
- Slot 编址规则：每个接收端对自身需要接收的变量建立全局 slot 表。宽度 ≤16 的变量占 1 个 slotId；宽度 >16 的变量按 16-bit 粒度切分为若干连续 slotId（低位片段占较低 slotId，高位递增）。Top 侧仅包含 topPorts 与 eModule，Top 自身共享一张表；SimWorker 侧仅包含 cModule 与 sModule，且每个分区的 SimWorker 维护独立的 slot 编址空间、独立表，不与其他分区共享或复用。
- 解码/写回：收帧时先读出 `slotId`，按硬编码表定位目标 verilator 变量与片偏移，将 `slotData` 写入对应 16-bit 切片。宽变量按片累积写回（多 slotId 组成一条宽信号），写入过程需支持重复帧的幂等性。
- 可靠性假设：MBus/SBus 不丢包但可能重复、乱序。接收端解码逻辑不依赖顺序，按 slotId 直接覆盖目标切片；同一周期的重复帧覆盖同一位置，确保幂等；跨周期的完整性仍由同步逻辑（ValueFlag）保障。CModel 当前实现为 FIFO 理想总线，不模拟乱序/重复。
- 发送侧约定：写侧按接收端 slot 表生成帧，不携带周期/阶段信息；顶层下发 I/Eo 时 targetId=各 worker，worker 上送 O/Ei 时 targetId=0，S→C 跨分区使用 SBus 且 targetId=目标分区+1。
- 虚方法数据流映射：
	- `TopModule::sendIAndEOutput()`：发送 I、Eo（MBus 下行）到 SimWorker 的 C 输入。
	- `TopModule::loadOAndEInput()`：从 SimWorker 的 C 输出接收 O、Ei（MBus 上行）。
	- `SimWorker::loadMBusCInputs()`：拉取 MBus 下行的 I/Eo。
	- `SimWorker::loadSBusCInputs()`：拉取 SBus 的远端 Si→Cj 输入。
	- `SimWorker::sendMBusCOutputs()`：发送本 worker C 输出的 O、Ei 上行到 Top（MBus 发）。
	- `SimWorker::copySInputs()`：读取本地 Ci→Si 输入，不经总线。
	- `SimWorker::sendSBusSOutputs()`：发送本 worker 的 S 阶段跨分区输出（SBus 发）。
	- `SimWorker::copyLocalCInputs()`：本地 Si→Ci 直连，不经总线。
- 虚方法伪代码模板（硬编码 slot，无查表）：
	- `TopModule::sendIAndEOutput()`：
		1. 对每个目标 worker，按硬编码 slotId 顺序从 topPorts/eModule 读出分片，打包 48-bit 帧。
		2. 在多条 mBusEndpoints 间简单轮询发送，分散负载（无动态查表）。
	- `TopModule::loadOAndEInput()`：
		1. 依次遍历每条 mBusEndpoint，先读出该端点的 `bufferCnt`，再严格读取该端点的全部帧（读完这一条再读下一条）。
		2. 使用硬编码的 switch/case 链按 slotId 直接写回 topPorts/eModule 切片；宽信号覆盖、重复帧幂等。
	- `SimWorker::loadMBusCInputs()`：
		1. 逐条读取绑定到本 worker 的 mBusEndpoints，查询 `bufferCnt` 后将该端点上的帧一次性读空，保持与发送侧相同顺序。
		2. 对每帧按 `[slotId, slotData]` 解码并走硬编码 switch/case，直接把 16-bit 分片写入对应的 C 输入缓冲，宽信号按片累积，重复帧覆盖同一片保证幂等。
	- `SimWorker::loadSBusCInputs()`：
		1. 在完成 MBus 拉取后遍历本分区订阅的 sBusEndpoints，按照 `bufferCnt` 将远端 S→C 帧全部取出。
		2. 仍以硬编码 slot 表写入 C 输入缓冲，只区分 SBus 源并保持 16-bit 粒度拼接，确保跨分区依赖在本轮 C 阶段前就绪。
	- `SimWorker::sendMBusCOutputs()`：
		1. 在 `cModule->eval()` 之后执行由生成器静态展开的 `sendMBusCOutputs` 序列，按编译期硬编码的 slot 顺序从 C 输出缓冲提取位片、拼装 48-bit 帧，`targetId` 固定为 0（Top）。
		2. 帧发送在 mBusEndpoints 间做简单轮询/分摊，逻辑同样在生成期固化，不依赖运行时查表。
	- `SimWorker::copySInputs()`：
		1. 生成器按 `copySInputs` 计划生成固定的 memcpy 片段，将本地 Ci→Si 直连对拷，直接从 C 输出缓冲读取分片写入 S 输入缓冲。
		2. 宽信号按 bitOffset 切片，顺序在代码中硬编码，不经过任何总线，保证 S 阶段 eval 前数据完备。
	- `SimWorker::sendSBusSOutputs()`：
		1. 在 `sModule->eval()` 之后走静态展开的 `sendSBusSOutputs` 序列，按记录的 `targetId`（目标分区+1）与 slotId 取出 S 输出分片、打包帧。
		2. 帧发送同样轮询本地 sBusEndpoints，所有目的端选择在代码中写死，只在运行时执行发送动作。
	- `SimWorker::copyLocalCInputs()`：
		1. 根据 `copyLocalCInputs` 计划在生成阶段直接产出固定的内存写回语句，把本地 Si→Ci 直连通路的输出片段写回下一轮 C 输入缓冲。
		2. 该步骤在同步标志上报之后执行，偏移与覆盖宽度均为硬编码，允许重复执行保持幂等。

# Corvus 代码生成方案指南

规划以下层次化数据结构，本着不重复存储数据的原则，例如位宽之类的信息不应该写入到 plan 中

- SlotRecvRecord 数据结构，记录通过MBus或者SBus接收的变量
	- portName：将要写入的端口名称
	- slotId：关联的 slotId
	- bitOffset：slotData将要写入的偏移
- SlotSendRecord 数据结构，记录通过MBus/SBus发送的变量
	- portName：将要读取的端口名称
	- bitOffset：从读取端口提取的开始偏移
	- targetId：目的地
	- slotId：目的地slotId
- CopyRecord 数据结构，记录本地内存拷贝的变量
	- portName：需要拷贝的端口名称
- TopModulePlan 数据结构
	- input：类型为 SlotSendRecord 数组
	- output：类型为 SlotRecvRecord 数组
	- externalInput：类型为 SlotRecvRecord 数组
	- externalOutput：类型为 SlotSendRecord 数组
- SimWorkerPlan 数据结构
	- loadMBusCInputs：SlotSendRecord 数组
	- loadSBusCInputs：SlotSendRecord 数组
	- sendMBusCOutputs：SlotSendRecord 数组
	- copySInputs：CopyRecord 数组
	- sendSBusSOutputs：SlotSendRecord 数组
	- copyLocalCInputs: CopyRecord 数组
- CorvusBusPlan 数据结构
	- topModulePlan：类型为 TopModulePlan
	- simWorkerPlans：类型为 SimWorkerPlan 数组

代码生成时，由 ConnectionAnalysis 生成 CorvusBusPlan，再由 CorvusBusPlan 生成总线操作代码

生成 connectionAnalysis 和 corvusBusPlan json 格式供核查。
