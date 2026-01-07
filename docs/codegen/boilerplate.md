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
- 文件：`boilerplate/corvus_cmodel/corvus_cmodel_sim_worker.{h,cpp}`
- 类：`CorvusCModelSimWorker`（继承 `CorvusSimWorker`）
- 生成器需补全以下虚方法的实现：`loadBusCInputs`、`sendCOutputsToBus`、`loadSInputs`、`sendSOutputs`、`loadLocalCInputs`。
- 构造函数已绑定 `cModule/sModule`、同步树端点以及 m/s 总线端点（均为 `std::shared_ptr`）。

## 顶层模块
- 文件：`boilerplate/corvus_cmodel/corvus_cmodel_top_module.{h,cpp}`
- 类：`CorvusCModelTopModule`（继承 `CorvusTopModule`）
- 生成器需补全以下虚方法的实现：`clearMBusRecvBuffer`、`sendIAndEOutput`、`loadOAndEInput`。
- 构造函数已绑定 eval 端、同步树端点以及 m/s 总线端点（均为 `std::shared_ptr`）。

## Worker 线程运行器
- 文件：`boilerplate/corvus_cmodel/corvus_cmodel_sim_worker_runner.{h,cpp}`
- 类：`CorvusCModelSimWorkerRunner`
- 已实现：构造函数接受 `vector<std::shared_ptr<CorvusCModelSimWorker>>`，`run` 为每个 worker 启动线程调用 `loop`，`stop` 取消并等待线程结束。无需生成代码。
