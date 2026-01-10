# 阶段性进展（当前）

- 生成链路：`CodeGenerator` 支持传入 `mbus_count`/`sbus_count`，并持久化模块列表供 `ConnectionBuilder` 使用；CLI 已新增 `--target` 选择 corvus/cmodel。
- 工件输出：`CorvusGenerator` 产出 JSON + 头文件（`C<output>TopModuleGen`/`C<output>SimWorkerGenP*`/`C<output>CorvusGen.h`，类名驼峰且含 output 前缀），MBus/SBus 端点数使用运行期 `assert` 校验。`CorvusCModelGenerator` 已落地，生成 `C<output>CModelGen.h` 并包装 top/worker 入口。
- 路由策略：基于接收端独立的 slot 空间，slotBits 取 8/16/32，48-bit payload 内选择 dataBits/chunkBits，实现宽信号分片；远端 S→C 路由按 targetId=分区+1。
- 共享工具：新增 `boilerplate/corvus/corvus_helper.h`，提供位掩码、payload 打包/解包、VlWide 读写与 SlotDecoder。
- 测试覆盖：`test_corvus_generator`（JSON/头文件烟测）、`test_corvus_slots`（跨分区 S→C）、`test_corvus_yuquan`（YuQuan 集成路径，需先生成 verilator 工件）、`test_corvus_yuquan_cmodel`（YuQuan CModel 集成）。

来自旧文档的补充
- 输入/约束与分类规则已经汇总到 `docs/architecture.md`。
- 编解码、收发时序细节已汇总到 `docs/workflow.md`。
- CLI/输出规则更新：拆分 `--output-dir` 和 `--output-name`，便于跨平台/路径管理；生成文件采用类名驼峰命名，避免与目录拼接混用下划线。

# 下一步
- 完善多仿真器解析（VCS/Modelsim 等尚未实现）。
- 丰富约束校验与错误提示，覆盖输入多 driver 等场景。
