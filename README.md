# Corvusitor

Corvusitor 从仿真产物中发现拓扑、做 corvus 语义分类，并生成可直接运行的 Corvus Top/SimWorker/CModel 封装代码。

- 当前仿真器支持：**仅 Verilator 已实现**（VCS/Modelsim 解析为占位未实现）。
- 约束：`corvus_comb_P*` 与 `corvus_seq_P*` 数量相同且 >0；`corvus_external` 最多 1 个；同名信号只能有单一 driver（多 driver 会被拒绝）。
- 生成物：`<output>_connection_analysis.json`、`<output>_corvus_bus_plan.json`、`C<output>TopModuleGen.{h,cpp}`、`C<output>SimWorkerGenP<ID>.{h,cpp}`、`C<output>CorvusGen.h`，`--target cmodel` 时额外 `C<output>CModelGen.h`。

## 构建
```bash
make            # 构建测试与 corvusitor 二进制
```

## 使用
```bash
./build/corvusitor \
  --module-build-dir <verilator_output_dir> \   # 或 --modules-dir
  --output-dir build \
  --output-name corvus_codegen \
  --mbus-count 8 \
  --sbus-count 8 \
  --target corvus            # 或 cmodel，默认为 corvus
```

更多细节见 `docs/architecture.md` 与 `docs/workflow.md`。

## 测试
```bash
make test_corvus_gen test_corvus_slots
# YuQuan 集成需先生成 verilator 工件：
# make test_corvus_yuquan
# make test_corvus_yuquan_cmodel
```
