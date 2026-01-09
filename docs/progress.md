# 阶段性进展（当前）

- 生成链路：`CodeGenerator` 支持传入 `mbus_count`/`sbus_count`，并持久化模块列表供 `ConnectionBuilder` 使用；CLI 新增对应参数。
- 工件输出：`CorvusGenerator` 同时产出 JSON 与头文件，生成 CorvusTopModuleGen/CorvusSimWorkerGenP*，对 MBus/SBus 端点数做编译期断言。
- 路由策略：基于接收端独立的 slot 空间，slotBits 取 8/16/32，48-bit payload 内选择 dataBits/ chunkBits，实现宽信号分片；远端 S→C 路由按 targetId=分区+1。
- 共享工具：新增 `boilerplate/corvus/corvus_codegen_utils.h`，提供位掩码、payload 打包/解包、VlWide 读写与 SlotDecoder。
- 测试覆盖：`test_corvus_generator`（JSON/头文件烟测）、`test_corvus_slots`（跨分区 S→C）、`test_corvus_yuquan`（YuQuan 集成路径，需先生成 verilator 工件）。

来自旧文档的补充
- 输入/约束与分类规则已经汇总到 `docs/architecture.md`。
- 编解码、收发时序细节已汇总到 `docs/workflow.md`。

后续方向
- 将 slot/路由策略做成可插拔，以适配不同总线容量/协议。
- 补充多分区、多端点分布的集成样例，提升回归覆盖度。
