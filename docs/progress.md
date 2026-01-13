# 阶段性进展（当前）

- 生成链路：`CodeGenerator` 支持传入 `mbus_count`/`sbus_count`，并持久化模块列表供 `ConnectionBuilder` 使用；CLI 已新增 `--target` 选择 corvus/cmodel。
- 工件输出：`CorvusGenerator` 产出 JSON + 头文件（`C<output>TopModuleGen`/`C<output>SimWorkerGenP*`/`C<output>CorvusGen.h`，类名驼峰且含 output 前缀），MBus/SBus 端点数使用运行期 `assert` 校验。`CorvusCModelGenerator` 已落地，生成 `C<output>CModelGen.h` 并包装 top/worker 入口。
- 路由策略：以接收端需求驱动的路由规划，远端 S→C 路由按 targetId=分区+1。
- 测试覆盖：`test_corvus_generator`（JSON/头文件烟测）、`test_corvus_slots`（跨分区 S→C）、`test_corvus_yuquan`（YuQuan 集成路径，需先生成 verilator 工件）、`test_corvus_yuquan_cmodel`（YuQuan CModel 集成）。

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
	- `SimWorker::loadRemoteCInputs()`：拉取 MBus 下行的 I/Eo 以及 SBus 的远端 Si→Cj 输入。
	- `SimWorker::sendRemoteCOutputs()`：发送本 worker C 输出的 O、Ei 上行到 Top（MBus 发）。
	- `SimWorker::loadSInputs()`：读取本地 Ci→Si 输入，不经总线。
	- `SimWorker::sendRemoteSOutputs()`：发送本 worker 的 S 阶段跨分区输出（SBus 发）。
	- `SimWorker::loadLocalCInputs()`：本地 Si→Ci 直连，不经总线。
- 虚方法伪代码模板（硬编码 slot，无查表）：
	- `TopModule::sendIAndEOutput()`：
		1. 对每个目标 worker，按硬编码 slotId 顺序从 topPorts/eModule 读出分片，打包 48-bit 帧。
		2. 在多条 mBusEndpoints 间简单轮询发送，分散负载（无动态查表）。
	- `TopModule::loadOAndEInput()`：
		1. 依次遍历每条 mBusEndpoint，先读出该端点的 `bufferCnt`，再严格读取该端点的全部帧（读完这一条再读下一条）。
		2. 使用硬编码的 switch/case 链按 slotId 直接写回 topPorts/eModule 切片；宽信号覆盖、重复帧幂等。
	- `SimWorker::loadRemoteCInputs()`：
		0. 对每条 mBusEndpoint、sBusEndpoint 分别获取 `bufferCnt`；对每条总线依次读满它的帧数（先读完一条再读下一条）。
		1. 读取过程中按硬编码 slotId 直接写入 cModule 输入切片（I/Eo 来自 MBus，Si→Cj 来自 SBus），宽信号按片覆盖。
	- `SimWorker::sendRemoteCOutputs()`：
		1. 按硬编码 slotId 从 cModule 输出读取 O/Ei 切片，打包 48-bit 帧。
		2. 在多条 mBusEndpoints 间简单轮询发送到 targetId=0，分散负载。
	- `SimWorker::loadSInputs()`：
		1. 依据硬编码映射执行 Ci→Si 内存拷贝（不经总线），宽信号按片拷贝。
	- `SimWorker::sendRemoteSOutputs()`：
		1. 按硬编码 slotId 读取 sModule 的跨分区输出切片，打包 48-bit 帧并发往对应 targetId（SBus）。
		2. 在多条 sBusEndpoints 间简单轮询发送，分散负载。
	- `SimWorker::loadLocalCInputs()`：
		1. 依据硬编码映射执行 Si→Ci 内存拷贝（不经总线），宽信号按片拷贝。

