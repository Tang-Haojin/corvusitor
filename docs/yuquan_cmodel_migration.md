# YuQuan 测试环境接入 CorvusCModel 迁移路线

目标：在本仓库（corvusitor）与随附的 `test/YuQuan` 样例内完成 CorvusCModel 迁移，明确需要修改的文件（含 `test/YuQuan/sim/src/sim_main_corvus.cpp`），确保生成、编译、联调和回归可控。

1) 基线与环境准备（作用范围限定在本仓库与 test/YuQuan）  
   - 拉取最新 corvusitor 代码，确认 `make` 可在本地通过（生成 corvusitor 可执行与测试用例）。  
   - YuQuan 工程可访问（本仓库自带 `test/YuQuan` 样例，可复用其 Makefile 流程），确保本地有 Verilator。  
   - 规划好 MBUS/SBUS 配置，避免与 YuQuan 实际分区数/链路不一致（默认 `MBUS_COUNT=8` / `SBUS_COUNT=3`，可按需覆盖）。

2) 生成 YuQuan 仿真工件（Verilator 输出）  
   - 在 corvusitor 根目录执行：`make -C test/YuQuan CORVUS=1 CORVUS_COMPILER_PATH=corvus-compiler verilate-archive`。  
   - 产物期望落在 `test/YuQuan/build/sim`，哨兵文件为 `verilator-compile-corvus_external/Vcorvus_external.h`（Makefile 的 `YUQUAN_SENTINEL`）。  
   - 若在外部 YuQuan 环境操作，可复用相同命令，或直接提供等效的 `--module-build-dir` 路径。

3) 通过 test/YuQuan 内部 Makefile 自动生成 CorvusCModel 产物（避免手动跑命令，且不依赖外层 Makefile）  
   - 在 `test/YuQuan` 目录下新增/修改生成规则（例如在主 Makefile 或 `sim_main_corvus.mk` 中添加 `yuquan_cmodel_gen` 目标），依赖已有的 verilate 产物；配方调用 corvusitor 二进制，路径从环境变量获取（默认使用 PATH 中的 `corvusitor`，如 `CORVUSITOR_BIN ?= corvusitor`，外部可通过 `CORVUSITOR_BIN=/path/to/corvusitor make ...` 覆盖）：  
     `$(CORVUSITOR_BIN) --module-build-dir=$(YUQUAN_SIM_DIR) --output-dir=$(CORVUSITOR_OUTPUT_DIR) --output-name=$(CORVUSITOR_CMODEL_OUTPUT_NAME) --mbus-count=$(MBUS_COUNT) --sbus-count=$(SBUS_COUNT) --target cmodel`。  
   - 将 `sim_main_corvus` 的编译依赖指向 `yuquan_cmodel_gen`，保证编译前自动生成 `CYuQuanCModelGen.h` 等文件；包含路径与变量沿用 YuQuan 内部 Makefile（输出目录默认 `test/YuQuan/build` 或由变量控制）。  
   - `make test_corvus_yuquan_cmodel` 仍可作为回归入口，但内部应通过环境变量传递 `CORVUSITOR_BIN`/`MBUS_COUNT`/`SBUS_COUNT`，复用同一生成目标，避免配置分叉。  
   - 预期产物：`build/YuQuan_corvus.json`、`build/CYuQuanCorvusGen.h`、`build/CYuQuanCModelGen.h` 及分区 worker 头文件；按需在 YuQuan 内部添加清理规则。

4) 在 test/YuQuan/sim/src/sim_main_corvus.cpp 的修改规划  
   - 引入新生成头：仅在源文件中 `#include "CYuQuanCModelGen.h"`（或与 `--output-name` 对应的前缀），不在代码中写绝对路径；头文件查找通过 Makefile 的 `-I` 路径解决。  
   - 构造入口：实例化 `corvus_generated::CYuQuanCModelGen cmodel;`，通过 `auto* top = cmodel.top();` / `auto* ports = cmodel.ports();` 获取顶层包装与端口访问。  
   - 时序驱动：保留现有时钟/复位节奏，将 `top->clock`/`top->reset` 等直连信号改写为对 `ports`（或 `top` 暴露的 API）的赋值，再调用 `cmodel.eval()` 代替 `top->eval()`；复位阶段仍需驱动 `ports` 中的 reset 信号。  
   - DIFFTEST/退出检测：原逻辑依赖 `top->io_*` 信号，需映射到 `ports` 中对应的顶层端口（生成头中字段名保持原顶层端口名，驼峰/下划线按生成物实际）。  
   - 波形/trace：如仍需 FST，可在 `CYuQuanCModelGen` 内部或外部加封装（当前 CModel 生成物不包含 Verilator 跟踪，若要保留可考虑单独对顶层模块包装 trace）。  
   - 构建脚本：在 `sim_main_corvus.mk` 中通过 `-I` 增加 `build`、`boilerplate/corvus_cmodel`、`boilerplate/corvus` 等包含路径，链接 pthread（CModel worker 线程需要），不在源文件写相对目录。  
   - 验证：先在 `test/YuQuan` 下编译运行 `sim_main_corvus`，确保新入口可跑通，DIFFTEST/退出状态正常。

5) 接入 YuQuan 测试/仿真环境（使用新 CModel）  
   - 在 YuQuan 测试工程中加入包含路径：`build`（生成物）、`boilerplate/corvus_cmodel` 与 `boilerplate/corvus`（基础类）。  
   - 以生成的 `corvus_generated::CYuQuanCModelGen`（类名前缀取决于 `--output-name`）为入口：`top()` 返回 top 封装，`ports()` 可直接访问顶层端口包装；构造函数内部已完成 bus/synctree 构建与 worker 线程启动。  
   - 若需要与现有仿真框架对齐 reset/循环，可在外层驱动 `eval()`（包含 `eval`+`evalE`）并根据需求扩展时钟/波形钩子。  
   - 如需静态链接或归档，请把生成头与 corvusitor 提供的 boilerplate 一并纳入构建系统。

6) 验证与回归  
   - 快速校验生成链路：`make test_corvus_yuquan_cmodel`（依赖上面生成的 YuQuan 仿真工件）。  
   - 生成后手动检查是否存在 `CYuQuanCModelGen.h`、`CYuQuanCorvusGen.h`、`YuQuan_corvus.json` 等关键文件，并确认类名与输出前缀一致。  
   - 在 YuQuan 测试环境运行既有用例/回归，make sim CORVUSITOR=1 REPCUT_NUM=8 BIN=microbench 应正确仿真

7) 发布与回滚策略  
   - 迁移时保留上一版本生成物或构建参数，必要时可切换 `--output-name` 到带版本号的前缀，便于双轨验证。  
   - 如发现 CModel 集成问题，可临时退回非 CModel 的 corvus 生成物（`--target corvus`）或恢复旧生成物，并记录触发的 MBUS/SBUS/分区配置以便进一步定位。  
   - 迁移完成后，将最终可复现的命令行、MBUS/SBUS 配置和产物路径固化到 YuQuan 测试环境文档/脚本中，便于后续重建。
