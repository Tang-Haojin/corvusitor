# 阶段性进展（当前）

- 生成链路：`CodeGenerator` 支持传入 `mbus_count`/`sbus_count`，并持久化模块列表供 `ConnectionBuilder` 使用；CLI 新增对应参数。
- 工件输出：`CorvusGenerator` 同时产出 JSON 与头文件，生成 `C<output>TopModuleGen`/`C<output>SimWorkerGenP*`（类名前缀带 output，文件名与类名一致的驼峰命名 `{.h,.cpp}`，聚合头为 `C<output>CorvusGen.h`），对 MBus/SBus 端点数做编译期断言。
- 路由策略：基于接收端独立的 slot 空间，slotBits 取 8/16/32，48-bit payload 内选择 dataBits/ chunkBits，实现宽信号分片；远端 S→C 路由按 targetId=分区+1。
- 共享工具：新增 `boilerplate/corvus/corvus_codegen_utils.h`，提供位掩码、payload 打包/解包、VlWide 读写与 SlotDecoder。
- 测试覆盖：`test_corvus_generator`（JSON/头文件烟测）、`test_corvus_slots`（跨分区 S→C）、`test_corvus_yuquan`（YuQuan 集成路径，需先生成 verilator 工件）。

来自旧文档的补充
- 输入/约束与分类规则已经汇总到 `docs/architecture.md`。
- 编解码、收发时序细节已汇总到 `docs/workflow.md`。
- CLI/输出规则更新：拆分 `--output-dir` 和 `--output-name`，便于跨平台/路径管理；生成文件采用类名驼峰命名，避免与目录拼接混用下划线。

# 下一步
目标：落地 CorvusCModelGenerator，产出可直接运行的 C++ CModel 入口，复用现有 CorvusGenerator 的分类/编解码逻辑，并将生成物拆分便于并行编译。

详细计划
- 架构与接口：确定产物命名（建议 `C<output>CModelGen.h`）和公开入口类 `C<output>CModelGen` 的 API（top/worker 访问、run/stop、reset/eval 流程），并决定继承/组合 CorvusGenerator 的方式以重用 slot 规划与 payload 编解码代码；Corvus 生成物已拆分为 `C<output>TopModuleGen.{h,cpp}`、`C<output>SimWorkerGenP<ID>.{h,cpp}`、聚合头 `C<output>CorvusGen.h`。
- 生成实现：
  - 复用已生成的 `C<output>TopModuleGen`/`C<output>SimWorkerGenP*`，在 `CorvusCModelGenerator` 中生成包装类 `C<output>CModelGen`。
  - 构造 `CorvusCModelSyncTree`（分区数即 simCore 数），获取 master/worker synctree 端点。
  - 为 MBus/SBus 创建 `CorvusCModelIdealizedBus`（端点数覆盖 top + workers），并对 `kCorvusGenMBusCount`/`kCorvusGenSBusCount` 做运行时断言。
  - 实例化 top + workers（shared_ptr 持有），注入 synctree + bus 端点；生成 runWorkers/stopWorkers（基于 `CorvusCModelSimWorkerRunner`）、reset/eval/evalE 以及 topPorts 访问的生命周期封装。
- CLI/构建：新增 cmodel 目标选择（如 `--target cmodel` 或 `--cmodel`），让 `CodeGenerator` 可切换到 `CorvusCModelGenerator`，保持原 Corvus 输出向后兼容；补充 make/test 目标与 include 路径使 cmodel 头可编译。
- 验证与测试：
  - 增加 cmodel 产物的生成快照/接口检查（类名、常量、公开方法）。
  - 基于 YuQuan 仿真输出跑 cmodel 集成测试：生成 `C<output>CModelGen.h`，校验存在 `C<output>CModelGen`/`C<output>TopModuleGen`/`C<output>SimWorkerGenP0` 等符号并能正常实例化，确保与 YuQuan 路径兼容；同时检查拆分文件（top/worker h+cpp）存在。
  - 补充最小运行冒烟：构造 `C<output>CModelGen`，启动 worker 线程，跑若干轮 eval + 总线交互，校验 sync flag、MBus/SBus 收发与 buffer 清理。
  - 回归现有 `test_corvus_generator`/`test_corvus_slots`/`test_corvus_yuquan`，确保新增入口不影响原生生成路径。
