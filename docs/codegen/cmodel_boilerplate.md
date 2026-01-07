# CorvusCModelIdealizedBus

- 实现理想化的 CModel 总线模型，只描述功能，不描述时序
- 代码写入 boilerplate/corvus_cmodel/corvus_cmodel_idealized_bus.h 和 boilerplate/corvus_cmodel/corvus_cmodel_idealized_bus.cpp
- 需要实现一个 CorvusCModelIdealizedBus 类（下称Bus类），以及一个继承自 BusEndpoint 的 CorvusCModelIdealizedBusEndpoint 类（下称Endpoint类）
- Bus 持有一个 vector<std::shared_ptr<CorvusCModelIdealizedBusEndpoint>> 以表示总线上的所有端点
- Endpoint 内建一个deque<uint64_t> 作为收发缓冲区
- 覆盖 Endpoint 的 send 虚方法，在 Bus 类的辅助下，将 payload 放入目标端点的接收缓冲区
- 覆盖 Endpoint 的 recv 虚方法，从自身的接收缓冲区中取出数据
- 覆盖 Endpoint 的 bufferCnt 和 clearBuffer 方法，分别返回缓冲区中数据的数量和清空缓冲区
- Bus 提供 Endpoint 的指针，之后注入到 CorvusCModelSimWorker 和 ModuleHandle 中使用
- Endpoint 的 buffer 写入要加锁保护，但读取不需要，读取时假设只有一个线程在操作
- Bus 构造时接受 endpointCount，提前创建所有 Endpoint，析构时负责释放
- 提供 getEndpointCount 方法查询端点总数，getEndpoint/getEndpoints 获取已有端点
- deliver 内部调用目标端点 enqueue，enqueue/clearBuffer 写入带锁，recv/ bufferCnt 读不加锁且缓冲区为空时 recv 返回 0

# CorvusCModelSyncTree
- 实现 CorvusCModelSyncTree 类、 CorvusCModelMasterSynctreeEndpoint 类和 CorvusCModelSimWorkerSynctreeEndpoint 类，放在 boilerplate/corvus_cmodel/corvus_cmodel_sync_tree.h 和 boilerplate/corvus_cmodel/corvus_cmodel_sync_tree.cpp
- CorvusCModelSyncTree 的构造函数接收一个 nSimCore 参数，配置仿真核的数量
- CorvusCModelSyncTree 创建一个 CorvusCModelMasterSynctreeEndpoint 作为主端点，创建 nSimCore 个 CorvusCModelSimWorkerSynctreeEndpoint 作为从端点
- CorvusCModelSyncTree 成员变量中包括一个 FlipFlag 类型的 masterSyncFlag 、 nSimCore 个 FlipFlag 类型的 simCoreCFinishFlag 和 nSimCore 个 FlipFlag 类型的 simCoreSFinishFlag
- CorvusCModelMasterSynctreeEndpoint 的 isMBusClear 和 isSBusClear 方法永远返回 true
- CorvusCModelMasterSynctreeEndpoint 的 getSimCoreCFinishFlag 方法在所有 simCoreCFinishFlag 达成一致时返回第一个标志，否则返回 PENDING
- CorvusCModelMasterSynctreeEndpoint 的 getSimCoreSFinishFlag 方法在所有 simCoreSFinishFlag 达成一致时返回第一个标志，否则返回 PENDING
- CorvusCModelMasterSynctreeEndpoint 的 setMasterSyncFlag 方法设置 masterSyncFlag
- CorvusCModelSimWorkerSynctreeEndpoint 的 setCFinishFlag 方法设置对应的 simCoreCFinishFlag
- CorvusCModelSimWorkerSynctreeEndpoint 的 setSFinishFlag 方法设置对应的 simCoreSFinishFlag
- CorvusCModelSimWorkerSynctreeEndpoint 的 getMasterSyncFlag 方法返回 masterSyncFlag

# CorvusCModelSimWorker

- 实现 CorvusCModelSimWorker 类，继承自 CorvusSimWorker，放在 boilerplate/corvus_cmodel/corvus_cmodel_sim_worker.h 和 boilerplate/corvus_cmodel/corvus_cmodel_sim_worker.cpp
- CorvusCModelSimWorker 的构造函数传入的参数列表及处理方法如下
    - ModuleHandle* cModule 和 ModuleHandle* sModule ：分别绑定在对应的基类指针上
    - std::shared_ptr<CorvusSimWorkerSynctreeEndpoint> simCoreSynctreeEndpoint ：绑定在对应的基类指针上
    - vector<std::shared_ptr<CorvusCModelIdealizedBusEndpoint>> mBusEndpoints 和 vector<std::shared_ptr<CorvusCModelIdealizedBusEndpoint>> sBusEndpoints ：将指针记录到成员变量中，供生成的函数调用
- 声明所有的虚方法，但不需要给出定义实现，之后由代码生成器生成具体实现

# CorvusCModelTopModule

- 实现 CorvusCModelTopModule 类，继承自 CorvusTopModule，放在 boilerplate/corvus_cmodel/corvus_cmodel_top_module.h 和 boilerplate/corvus_cmodel/corvus_cmodel_top_module.cpp
- CorvusCModelTopModule 的构造函数传入的参数列表及处理方法如下
    - ModuleHandle* eModule 绑定在对应的基类指针上
    - std::shared_ptr<CorvusTopSynctreeEndpoint> masterSynctreeEndpoint 绑定在对应的基类指针上
    - vector<std::shared_ptr<CorvusCModelIdealizedBusEndpoint>> mBusEndpoints 和 vector<std::shared_ptr<CorvusCModelIdealizedBusEndpoint>> sBusEndpoints ：将指针记录到成员变量中，供生成的函数调用
- 声明所有的虚方法，但不需要给出定义实现，之后由代码生成器生成具体实现

# CorvusCModelSimWorkerRunner

- 实现 CorvusCModelSimWorkerRunner 类，放在 boilerplate/corvus_cmodel/corvus_cmodel_sim_worker_runner.h 和 boilerplate/corvus_cmodel/corvus_cmodel_sim_worker_runner.cpp
- CorvusCModelSimWorkerRunner 的构造函数接收以下参数
    - vector<std::shared_ptr<CorvusCModelSimWorker>> simWorkers ：所有 simWorker 的指针
- CorvusCModelSimWorkerRunner 提供 run 方法，为每一个 CorvusCModelSimWorker 创建一个线程并运行 loop 方法，run 方法不等待线程结束即返回
- CorvusCModelSimWorkerRunner 提供 stop 方法，杀死所有仿真核的线程