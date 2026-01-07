# Corvus CModel 代码生成概要

针对 corvus_cmodel 目录的代码生成器需要补全下列骨架，注意全部使用 `std::shared_ptr` 管理 bus/synctree endpoint。

## 总线模型
- 文件：`boilerplate/corvus_cmodel/corvus_cmodel_idealized_bus.{h,cpp}`
- 类：`CorvusCModelIdealizedBus`、`CorvusCModelIdealizedBusEndpoint`
- 已提供功能骨架，若需要扩展可在此基础上生成代码。

## 同步树
- 文件：`boilerplate/corvus_cmodel/corvus_cmodel_sync_tree.{h,cpp}`
- 类：`CorvusCModelSyncTree`、`CorvusCModelMasterSynctreeEndpoint`、`CorvusCModelSimWorkerSynctreeEndpoint`
- 已实现共享指针管理的端点创建/查询逻辑，无需生成代码。

## 仿真核 Worker
- 文件：`boilerplate/corvus/corvus_sim_worker.{h,cpp}`
- 类：`CorvusSimWorker`
- 生成器需补全以下虚方法的实现：`createSimModules`（负责创建并填充 `cModule/sModule`）、`deleteSimModules`（负责释放它们）、`loadBusCInputs`、`sendCOutputsToBus`、`loadSInputs`、`sendSOutputs`、`loadLocalCInputs`。
- 提供 `init`/`cleanup` 以调用上述创建/销毁逻辑，构造函数仅接收同步树端点以及 m/s 总线端点（均为裸指针），不会自动调用创建逻辑。

## 顶层模块
- 文件：`boilerplate/corvus/corvus_top_module.{h,cpp}`
- 类：`CorvusTopModule`
- 生成器需补全以下虚方法的实现：`createExternalModule`（负责创建并填充 `eHandle`）、`deleteExternalModule`（负责释放它）、`sendIAndEOutput`、`loadOAndEInput`、`resetSimWorker`（替代原 `init` 行为）。`clearMBusRecvBuffer` 已默认清空所有 mBusEndpoints，方法为私有。
- 提供 `init`/`cleanup` 以调用外设模块与顶层端口的创建/销毁逻辑；构造函数仅接收同步树端点以及 m/s 总线端点（均为裸指针），不会自动调用创建逻辑。

## Worker 线程运行器
- 文件：`boilerplate/corvus_cmodel/corvus_cmodel_sim_worker_runner.{h,cpp}`
- 类：`CorvusCModelSimWorkerRunner`
- 已实现：构造函数接受 `vector<std::shared_ptr<CorvusSimWorker>>`，`run` 为每个 worker 启动线程调用 `loop`，`stop` 取消并等待线程结束。无需生成代码。
