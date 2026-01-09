# Corvusitor 工作流

## 前置条件
- 已生成的仿真模块（verilator/gsim）：`corvus_comb_P*`、`corvus_seq_P*`、可选 `corvus_external`，端口名称/位宽满足 corvus 约束（见 `docs/architecture.md#输入约束来自仿真输出`）。
- 可选：配置 `MBUS_COUNT` / `SBUS_COUNT`（编译期总线端点数）。

## 使用步骤
1) 准备模块目录  
   `modules_dir` 指向仿真输出（包含各模块头文件）。  
2) 运行 corvusitor  
```bash
./build/corvusitor \
  --modules-dir <sim_output_dir> \
  --output-name corvus_codegen \
  --mbus-count 8 \
  --sbus-count 8
```
3) 产物  
   - `<output>_corvus.json`：`ConnectionAnalysis` 快照，可作为其他生成/检查输入。  
   - `<output>_corvus_gen.h`：包含 CorvusTopModuleGen/CorvusSimWorkerGenP* 的实现，可直接编译进上层模拟。

## 运行时数据流（一个周期内）
- Top → Worker（MBus）：`sendIAndEOutput` 将 I/Eo 按 slot+chunk 编码（round-robin 分配到 mBusEndpoints），`targetId` 为目标分区+1。
- Worker → Top（MBus）：`sendCOutputsToBus` 将 O/Ei 发送到 `targetId=0`；Top 在 `loadOAndEInput` 解码写回 TopPorts / external。
- Worker ↔ Worker（SBus）：`sendSOutputs` 将 `remote_s_to_c` 发送到目标分区；`loadBusCInputs`/`loadSInputs` 轮询 sBusEndpoints 解码拼装。
- 本地直连：`loadSInputs` 负责 Ct→Si 拷贝，`loadLocalCInputs` 负责 St→Ci 拷贝，避免上总线。
- 接收端必须耗尽所有端点的 `bufferCnt`，基于 slotId+chunkIdx 重组；VlWide 通过 `corvus_codegen_utils` 做跨 word 处理。

## 编解码与 slot 规划细节（供调试）
- slot 空间按“接收端”独立规划：Top/每个 Worker 只为自己要接收的信号分配 slotId；发送端按目标的 slotId 发送。
- slotBits=ceil(log2(slotCount)) 再向上取整到 {8,16,32}。
- dataBits 选择不超过 (48-slotBits-chunkBits) 的最大值（优先 32/16/8）；若 width 仍超出则提升 chunkBits=8/16/32，并重算 chunkCount=ceil(width/dataBits)。
- payload 布局：`chunkIdx | data | slotId`，slotId 在最低位；未分片时 chunkBits=0。
- MBus/SBus 不保证顺序但保证不重不漏，接收端需要基于 slotId+chunkIdx 组帧；正常流程下每轮 buffer 会被耗尽，无需额外 clear。

## 验证与调试
- 快速校验：`make test_corvus_gen`（JSON/头文件生成烟测），`make test_corvus_slots`（跨分区 S→C 收发），`make test_corvus_yuquan`（需 YuQuan 工件）。
- 若 slot/路由异常，可检查生成头文件中的 slotBits/chunkBits 分配与 targetId，或直接查看 JSON 中的连接分类结果。
