# Corvus Codegen Pipeline (v2)

本阶段的升级将 pipeline 拓展为“分析 + 工件生成”两步：`CodeGenerator` 负责模块发现、连接分析与参数收集，`CorvusGenerator` 直接产出可用的 JSON 与 C++ 头文件（含 Top/Worker 实现），并可在编译期配置 MBus/SBus 路由。

## 核心组件
- `ConnectionAnalysis`：包含顶层 I/O、Ei/Eo，以及按分区划分的 localCtSi、localStCi、remoteSitCj。
- `CodeGenerator`：负责模块发现/解析、连接分析，并持久化模块列表以保证分析指针有效；接受 `mbus_count`/`sbus_count` 用于后续路由规划。
- `CorvusGenerator`：当前唯一的 target generator，输出 `<output_base>_corvus.json` 以及 `<output_base>_corvus_gen.h`。头文件内生成 CorvusTopModuleGen 与各分区的 CorvusSimWorkerGenP*，带完整的 MBus/SBus 收发逻辑。
- `boilerplate/corvus/corvus_codegen_utils.h`：生成代码使用的共享工具，提供位掩码、payload 打包、VlWide 读写与 SlotDecoder。

## 使用
```bash
./build/corvusitor \
  --modules-dir <sim_output_dir> \
  --output-name corvus_codegen \
  --mbus-count 8 \
  --sbus-count 8
# 输出 corvus_codegen_corvus.json 与 corvus_codegen_corvus_gen.h
```

## 生成物
- JSON：序列化的 `ConnectionAnalysis`，字段包含 `top_inputs` / `top_outputs` / `external_inputs` / `external_outputs` / `partitions`（含 `local_cts_to_si`、`local_stc_to_ci`、`remote_s_to_c`）与 `warnings`。每个连接项含 `port`、`width`、`width_type`（`PortWidthType` 枚举值）、`driver`、`receivers`（module/port 名）。
- 头文件： 
  - Top 侧的 `CorvusTopModuleGen`/`TopPortsGen`，实现 I/Ei/Eo/O 的 MBus 编码/解码。
  - Worker 侧的 `CorvusSimWorkerGenP*`，实现本地 Ct→Si / St→Ci 直连、远端 S→C 的 SBus 解码，以及 C→Top/O/Ei 的 MBus 发送。
  - 内置静态常量 `kCorvusGenMBusCount` / `kCorvusGenSBusCount`，对终端数进行断言，使用 slot+chunk 规划对宽信号做分片。

## 路由与 slot 策略
- 按连接数计算 slot bits（8/16/32），对每个信号计算 chunk 计划（优先 32/16/8 bits，满足 48-bit payload 约束），并均匀分布到 `mbus_count` / `sbus_count` 端点。
- MBus：Top 下发 I/Eo，Worker 上送 O/Ei；按 targetId=分区号+1 发送。
- SBus：分区间远端 S→C；发送端按接收分区 targetId 路由。
- VlWide 信号使用 `corvus_codegen_utils` 进行跨 word 的拆装，标量信号直接掩码/移位。

## 测试/验证
- 单元测试：`test_corvus_generator` 覆盖 JSON 与头文件生成；`test_corvus_slots` 覆盖分区跨 SBus 的 remote S→C 规划与 worker 生成；`test_corvus_yuquan` 针对 YuQuan 仓库的集成路径（需先生成 verilator 工件，可通过 `make yuquan_build`）。

## 下一步想法
- 提取 slot 策略为可插拔策略，便于针对不同 bus 容量/协议扩展。
- 增加更多集成测试样本（多分区、多宽度、异构端点数量）。 
