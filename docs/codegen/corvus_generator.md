# Corvus Codegen Pipeline (v2)

This upgrade introduces an object抽象的代码生成架构：`CodeGenerator` 通过 `ConnectionBuilder::analyze` 生成 `ConnectionAnalysis`，然后由 `CorvusGenerator` 负责产出面向 corvus 的工件。

## 核心组件
- `ConnectionAnalysis`：包含顶层 I/O、Ei/Eo，以及按分区划分的 localCtSi、localStCi、remoteSitCj。
- `CodeGenerator`：负责模块发现/解析、连接分析，并将结果交给目标生成器。
- `CorvusGenerator`：当前唯一的 target generator，输出 `<output_base>_corvus.json`，内容为上述结构的 JSON 序列化，可作为后续 bus/slot 编码及代码发射的输入。

## 使用
```bash
./build/corvusitor --modules-dir <sim_output_dir> --output-name corvus_codegen
# 生成 corvus_codegen_corvus.json
```

## 产物格式
- `top_inputs` / `top_outputs` / `external_inputs` / `external_outputs`
- `partitions`: 每个分区下的 `local_cts_to_si`、`local_stc_to_ci`、`remote_s_to_c`
- `warnings`: 分析过程中的非致命提示

每个连接条目包含 `port`, `width`, `width_type`（同 `PortWidthType` 枚举的整数值）、`driver`、`receivers`（module/port 名）。

## 后续
- 基于 JSON 构建 bus slot 分配与具体 C++ 代码生成（CorvusTopModuleGen / CorvusSimWorkerGenP*）。
- 引入策略型接口，支持不同目标（如 cmodel、其他总线实现）的 codegen。
