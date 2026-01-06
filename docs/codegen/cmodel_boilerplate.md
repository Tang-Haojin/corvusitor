# IdealizeCModelBus

- 实现理想化的 CModel 总线模型，只描述功能，不描述时序
- 代码写入 boilerplate/cmodel/include/idealized_cmodel_bus.h 和 boilerplate/cmodel/idealized_cmodel_bus.cpp
- 需要实现一个 IdealizedCModelBus 类（下称Bus类），以及一个继承自 BusEndpoint 的 IdealizedCModelBusEndpoint 类（下称Endpoint类）
- Bus 持有一个 vector<IdealizedCModelBusEndpoint*> 以表示总线上的所有端点
- Endpoint 内建一个deque<uint64_t> 作为收发缓冲区
- 覆盖 Endpoint 的 send 虚方法，在 Bus 类的辅助下，将 payload 放入目标端点的接收缓冲区
- 覆盖 Endpoint 的 recv 虚方法，从自身的接收缓冲区中取出数据
- 覆盖 Endpoint 的 bufferCnt 和 clearBuffer 方法，分别返回缓冲区中数据的数量和清空缓冲区
- Bus 提供 Endpoint 的指针，之后注入到 SimCoreApp 和 ModuleHandle 中使用
- Endpoint 的 buffer 写入要加锁保护，但读取不需要，读取时假设只有一个线程在操作
- Bus 构造时接受 endpointCount，提前创建所有 Endpoint，析构时负责释放
- 提供 getEndpointCount 方法查询端点总数，getEndpoint/getEndpoints 获取已有端点
- deliver 内部调用目标端点 enqueue，enqueue/clearBuffer 写入带锁，recv/ bufferCnt 读不加锁且缓冲区为空时 recv 返回 0

# CModelSyncTree
- 实现 CModelSyncTree 类、 CModelMasterSynctreeEndpoint 类和 SimCoreSynctreeEndpoint 类，放在 boilerplate/cmodel/include/cmodel_sync_tree.h 和 boilerplate/cmodel/cmodel_sync_tree.cpp
- CModelSyncTree 的构造函数接收一个 nSimCore 参数，配置仿真核的数量
- CModelSyncTree 创建一个 CModelMasterSynctreeEndpoint 作为主端点，创建 nSimCore 个 SimCoreSynctreeEndpoint 作为从端点
- CModelSyncTree 成员变量中包括一个 FlipFlag 类型的 masterSyncFlag 、 nSimCore 个 FlipFlag 类型的 simCoreCFinishFlag 和 nSimCore 个 FlipFlag 类型的 simCoreSFinishFlag
- CModelMasterSynctreeEndpoint 的 isMBusClear 和 isSBusClear 方法永远返回 true
- CModelMasterSynctreeEndpoint 的 getSimCoreCFinishFlag 方法在所有 simCoreCFinishFlag 达成一致时返回第一个标志，否则返回 PENDING
- CModelMasterSynctreeEndpoint 的 getSimCoreSFinishFlag 方法在所有 simCoreSFinishFlag 达成一致时返回第一个标志，否则返回 PENDING
- CModelMasterSynctreeEndpoint 的 setMasterSyncFlag 方法设置 masterSyncFlag
- SimCoreSynctreeEndpoint 的 setCFinishFlag 方法设置对应的 simCoreCFinishFlag
- SimCoreSynctreeEndpoint 的 setSFinishFlag 方法设置对应的 simCoreSFinishFlag
- SimCoreSynctreeEndpoint 的 getMasterSyncFlag 方法返回 masterSyncFlag

# CModelSimCoreApp

- 实现 CModelSimCoreApp 类，继承自 SimCoreApp，放在 boilerplate/cmodel/include/cmodel_sim_core_app.h 和 boilerplate/cmodel/cmodel_sim_core_app.cpp
- CModelSimCoreApp 的构造函数传入的参数列表及处理方法如下
    - ModuleHandle* cModule 和 ModuleHandle* sModule ：分别绑定在对应的基类指针上
    - SimCoreSynctreeEndpoint* simCoreSynctreeEndpoint ：绑定在对应的基类指针上
    - vector<IdealizedCModelBusEndpoint*> mBusEndpoints 和 vector<IdealizedCModelBusEndpoint*> sBusEndpoints ：将指针记录到成员变量中，供生成的函数调用
- 声明所有的虚方法，但不需要给出定义实现，之后由代码生成器生成具体实现

# CModelMasterHandle

- 实现 CModelMasterHandle 类，继承自 ModuleHandle，放在 boilerplate/cmodel/include/cmodel_master_handle.h 和 boilerplate/cmodel/cmodel_master_handle.cpp
- CModelMasterHandle 的构造函数传入的参数列表及处理方法如下
    - ModuleHandle* eModule 绑定在对应的基类指针上
    - SimCoreSynctreeEndpoint* masterSynctreeEndpoint 绑定在对应的基类指针上
    - vector<IdealizedCModelBusEndpoint*> mBusEndpoints 和 vector<IdealizedCModelBusEndpoint*> sBusEndpoints ：将指针记录到成员变量中，供生成的函数调用
- 声明所有的虚方法，但不需要给出定义实现，之后由代码生成器生成具体实现

# CModelSimCoreAppRunner

- 实现 CModelSimCoreAppRunner 类，放在 boilerplate/cmodel/include/cmodel_sim_core_app_runner.h 和 boilerplate/cmodel/cmodel_sim_core_app_runner.cpp
- CModelSimCoreAppRunner 的构造函数接收以下参数
    - vector<CModelSimCoreApp*> simCoreApps ：所有仿真核的指针
- CModelSimCoreAppRunner 提供 run 方法，为每一个 CModelSimCoreApp 创建一个线程并运行 loop 方法，run 方法不等待线程结束
- CModelSimCoreAppRunner 提供 stop 方法，杀死所有仿真核的线程
