# Corvusitor 工作流

## 前置条件
- Verilator 仿真输出目录（形如 `verilator-compile-<module>/V<module>.h`）；遇到 VCS/Modelsim 输出会报 “parser not implemented” 后退出。
- 模块命名/数量：`corvus_comb_P*`、`corvus_seq_P*` 必须等量且 >0，可选 `corvus_external` ≤1。
- 拓扑约束：同名端口单 driver、位宽一致；无 driver 仅能喂给 COMB；COMB 只能驱动同分区 SEQ 或 external；SEQ 只能驱动 COMB（可跨分区）；EXTERNAL 只能驱动 COMB。违例在分析阶段直接抛错，不以 warning 继续。
- 可选：`mbus_count`/`sbus_count`（编译期路由），输出前缀由 `--output-dir` + `--output-name` 拼成。

## 使用步骤
1) 准备模块目录：`--modules-dir` 指向仿真输出根目录；如使用 `--module-build-dir` 会覆盖前者。  
2) 运行 corvusitor：
```bash
./build/corvusitor \
  --module-build-dir <sim_output_dir> \  # 或使用 --modules-dir
  --output-dir build \
  --output-name corvus_codegen \
  --mbus-count 8 \
  --sbus-count 8 \
  --target corvus            # 或 cmodel，默认为 corvus
```
类名前缀从 `<output-name>` 派生（做路径基名、字符清洗后前缀 `C`）；输出文件前缀为 `<output-dir>/<output-name>`。
3) 产物  
   - `<output>_connection_analysis.json`：`ConnectionAnalysis` 快照，可供诊断或下游生成。  
   - `<output>_corvus_bus_plan.json`：`CorvusBusPlan`，记录 slot 编址与拷贝计划。  
   - `C<output>TopModuleGen.{h,cpp}`：TopPorts/TopModule 实现。  
   - `C<output>SimWorkerGenP<ID>.{h,cpp}`：每分区一个 Worker 实现。  
   - `C<output>CorvusGen.h`：聚合头，包含所有 top/worker。  
   - `--target cmodel` 时额外生成 `C<output>CModelGen.h`（CModel 入口，包装同步树/总线/线程）。 

## 运行时数据流（一个周期内）
- Top → Worker（MBus）：`sendIAndEOutput` 下发 I/Eo，`targetId`=分区+1，轮询多条 MBus 端点发送。
- Worker → Top（MBus）：`sendMBusCOutputs` 将 O/Ei 上送 `targetId=0`；Top 在 `loadOAndEInput` 将 payload 解码回 `TopPortsGen`/external。
- Worker ↔ Worker（SBus）：`sendSBusSOutputs` 将跨分区 `remote_s_to_c` 发送到 `targetId`=目标分区+1；`loadSBusCInputs` 拉取并写回 comb。
- 本地直连：`copySInputs` 负责 Ct→Si 拷贝，`copyLocalCInputs` 负责 St→Ci 拷贝，不占用总线。
- 帧格式固定 48-bit（slotId|16-bit slice），Top/Worker slot 空间互相独立；宽信号按 16-bit 片扩展，slotId 固化在生成代码中。

## 同步时序（以 `boilerplate/corvus` 为准）
- Top 周期：
  1. `sendIAndEOutput()`；  
  2. 等待 `isMBusClear()` 与 `isSBusClear()`；  
  3. `raiseTopSyncFlag()`；  
  4. 等待 `isSimWorkerInputReadyFlagRaised()`；  
  5. `raiseTopAllowSOutputFlag()`；  
  6. 等待 `isMBusClear()` 且 `isSimWorkerSyncFlagRaised()`；  
  7. `loadOAndEInput()`。
- Worker 周期：
  1. 自旋等待 `START_GUARD`（`prepareSimWorker()` 设置）；  
  2. 等待 `isTopSyncFlagRaised()`；  
  3. `loadMBusCInputs()` → `loadSBusCInputs()` → `raiseSimWorkerInputReadyFlag()`；  
  4. `cModule->eval()` → `sendMBusCOutputs()` → `copySInputs()`；  
  5. `sModule->eval()` → 等待 `isTopAllowSOutputFlagRaised()` → `sendSBusSOutputs()`；  
  6. `raiseSimWorkerSyncFlag()` → `copyLocalCInputs()`；  
  7. 受 `loopContinue` 控制是否进入下一轮。
- ValueFlag：8-bit 环形计数，0 为 pending，`nextValue()` 跳过 0。Top/Worker 如观测到旗标跳变到非预期值会持续打印 fatal 信息。

## 验证与调试
- 快速校验：`make test_corvus_gen`（生成烟测）、`make test_corvus_slots`（跨分区 S→C）、`make test_corvus_yuquan`/`make test_corvus_yuquan_cmodel`（需先在 `test/YuQuan` 下生成 Verilator 工件）。
- 路由/slot 异常：可检查 JSON 或生成代码中的 slotId/targetId。
