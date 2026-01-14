# Corvusitor 工作流

## 前置条件
- 已生成的 Verilator 仿真模块（当前仅实现 Verilator，其余仿真器支持尚未实现；目录中出现 VCS/Modelsim 等未支持产物会报错退出）：`corvus_comb_P*`、`corvus_seq_P*`、可选 `corvus_external`，端口名称/位宽满足 corvus 约束（见 `docs/architecture.md#输入约束来自仿真输出`）。
- 约束违规（多 driver、位宽不一致、非法跨分区或 external 连接等）会在解析/分类阶段直接报错终止，不再以 warning 继续。
- 可选：配置 `MBUS_COUNT` / `SBUS_COUNT`（编译期总线端点数）。

## 使用步骤
1) 准备模块目录  
   `modules_dir` 指向仿真输出（包含各模块头文件）。  
2) 运行 corvusitor  
```bash
./build/corvusitor \
  --module-build-dir <sim_output_dir> \  # 或使用 --modules-dir
  --output-dir build \
  --output-name corvus_codegen \
  --mbus-count 8 \
  --sbus-count 8 \
  --target corvus            # 或 cmodel，默认为 corvus
```
3) 产物  
   - `<output>_connection_analysis.json`：`ConnectionAnalysis` 快照，可作为其他生成/检查输入。  
   - `<output>_corvus_bus_plan.json`：由 ConnectionAnalysis 推导的 `CorvusBusPlan`，记录 slot 编址与拷贝计划。  
   - `C<output>TopModuleGen.{h,cpp}`：TopPorts/TopModule 实现。  
   - `C<output>SimWorkerGenP<ID>.{h,cpp>`：每个分区一个 worker 实现。  
   - `C<output>CorvusGen.h`：聚合头，包含所有 top/worker 头。  
   - 选择 `--target cmodel` 时额外生成 `C<output>CModelGen.h`（CModel 入口）。 

> 说明：`--output-dir` 控制输出目录，`--output-name` 控制输出前缀（决定类名/文件名中的 `<output>` 部分），二者拼成完整路径，不再将目录与名称混用。生成文件名与类名保持一致的驼峰风格，便于跨平台。

## 运行时数据流（一个周期内）
- Top → Worker（MBus）：`sendIAndEOutput` 将 I/Eo 分发到 mBusEndpoints，`targetId` 为目标分区+1。
- Worker → Top（MBus）：`sendRemoteCOutputs` 将 O/Ei 发送到 `targetId=0`；Top 在 `loadOAndEInput` 写回 TopPorts / external。
- Worker ↔ Worker（SBus）：`sendRemoteSOutputs` 将 `remote_s_to_c` 发送到目标分区；`loadRemoteCInputs`/`loadSInputs` 轮询 mBus/sBus 端点汇聚。
- 本地直连：`loadSInputs` 负责 Ct→Si 拷贝，`loadLocalCInputs` 负责 St→Ci 拷贝，避免上总线。

## 同步时序（当前实现）
- 顶层 Top 周期（参考 [boilerplate/corvus/corvus_top_module.cpp](boilerplate/corvus/corvus_top_module.cpp)）
   1. `sendIAndEOutput()` 下发本轮输入与 external 输出。
   2. 等待 `isMBusClear()` 与 `isSBusClear()` 均为真（上轮残留耗尽）。
   3. `raiseTopSyncFlag()`：`topSyncFlag.updateToNext()`，调用 `setTopSyncFlag(topSyncFlag)`。
   4. 等待 MBus 清空与所有 Worker S 完成一致：`getSimWorkerSFinishFlag()` == `prevSFinishFlag.nextValue()`，成功后 `prevSFinishFlag.updateToNext()`。
   5. `loadOAndEInput()` 接收本轮输出与 external 输入。

- Worker 周期（参考 [boilerplate/corvus/corvus_sim_worker.cpp](boilerplate/corvus/corvus_sim_worker.cpp)）
   1. 轮询 `getTopSyncFlag()`，当等于 `prevTopSyncFlag.nextValue()` 时进入本轮并 `prevTopSyncFlag.updateToNext()`。
   2. C 阶段：`loadRemoteCInputs()` → `cModule->eval()` → `sendRemoteCOutputs()`。
   3. S 阶段：`loadSInputs()` → `sModule->eval()` → `sendRemoteSOutputs()`。
   4. 完成上报：`raiseSFinishFlag()`，将本地 `sFinishFlag` 提升并 `setSFinishFlag(sFinishFlag)`；随后 `loadLocalCInputs()` 执行本地 St→Ci 拷贝。
   5. 循环尾：记录阶段日志，进入下一轮等待 Top 同步变化。

- 辅助/初始化
   - 顶层在启动前调用 `prepareSimWorker()`：`setSimWorkerStartFlag(ValueFlag::START_GUARD)` 作为 Worker 启动哨兵；Worker 提供 `hasStartFlagSeen()` 用于检测，但当前主循环以 Top 同步为驱动。
   - `ValueFlag` 环形计数语义：保留 `0` 为 PENDING，驱动时使用 `nextValue()`，详见 [boilerplate/corvus/corvus_synctree_endpoint.h](boilerplate/corvus/corvus_synctree_endpoint.h)。

- 错误与一致性
   - Worker 若观测到 `getTopSyncFlag()` 出现非期望跳变（既非当前也非 `nextValue()`）将输出致命错误并 `stop()`；顶层若观测到 S 完成标志跳变异常将 `exit(1)`。
   - 顶层对 S 完成的判定为“全体一致”语义，未达一致返回 `0`（PENDING），因此等待可能跨越多个 Worker 的异步完成窗口。

## 验证与调试
- 快速校验：`make test_corvus_gen`（JSON/头文件生成烟测），`make test_corvus_slots`（跨分区 S→C 收发），`make test_corvus_yuquan`（需 YuQuan 工件）。
- 若路由异常，可检查生成头文件中的 targetId，或直接查看 JSON 中的连接分类结果。
