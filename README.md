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

## YuQuan 集成脚本
- 预备仿真工件：在仓库根目录运行 `make yuquan_build`（会在 `test/YuQuan` 下调用 verilate-archive，默认使用 `corvus-compiler`，可用 `YUQUAN_CORVUS_COMPILER_PATH=/path/to/corvus-compiler` 覆盖）。
- 运行集成测试：  
  - Corvus 目标：`make test_corvus_yuquan MBUS_COUNT=8 SBUS_COUNT=8 MODULE_BUILD_DIR=test/YuQuan/build/sim YUQUAN_OUTPUT_DIR=test/YuQuan/build YUQUAN_OUTPUT_NAME=YuQuan`  
  - CModel 目标：`make test_corvus_yuquan_cmodel MBUS_COUNT=8 SBUS_COUNT=8 MODULE_BUILD_DIR=test/YuQuan/build/sim YUQUAN_CMODEL_OUTPUT_DIR=test/YuQuan/build YUQUAN_CMODEL_OUTPUT_NAME=YuQuan`
- 目标含义：`test_corvus_yuquan` 生成并验证 corvus 目标（Top/SimWorker 头文件 + JSON）与 YuQuan 仿真对接；`test_corvus_yuquan_cmodel` 则生成并验证 cmodel 目标（额外 CModel 封装，idealized bus + worker 线程）。
- 直接驱动 YuQuan 仿真（无需手动生成 Makefile）：`make yuquan_corvus_sim MBUS_COUNT=8 SBUS_COUNT=8` 会在 `test/YuQuan/build/sim/corvusitor-compile` 下生成并编译 corvusitor 输出，然后运行 YuQuan 仿真。可用 `CORVUSITOR_BIN`、`CORVUSITOR_MBUS_COUNT`、`CORVUSITOR_SBUS_COUNT` 覆盖。
- 相关默认变量可在 `test/YuQuan/sim/src/corvus_vars.mk` 查看；YuQuan 根目录为 `test/YuQuan`。
