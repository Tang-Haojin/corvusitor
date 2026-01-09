# Corvusitor Workflow

## 整体流程

- 输入 verilator/gsim 仿真器编译产生的一组 module
- 分析 module 的类型、接口
- 分析接口连接关系
- 根据接口连接关系生成代码

## Corvusitor 架构要求

- 对应输入module的分析、接口连接关系整理部分代码应当共享
- 代码生成器部分形成类继承层次结构，针对不同生成需求形成不同子类

## Corvusitor 输入的特点

- 输入为一组模块，由 verilator 等仿真器编译形成的 C/C++ 代码库，分为三类 corvus_comb_P*、corvus_seq_P* 和 corvus_external
- corvus_external 1 个， corvus_comb_P*、corvus_seq_P* 数量相同，均为 N 个
- 模块间的连接关系由端口方向和名称决定
    - 连接在一起的端口具有相同的名称
    - 连接在一起的端口具有相同的位宽，否则为非法
    - 一个输出端口可以驱动0到多个输入端口
        - 如果输出端口驱动的输入端口数量为 0，那么它是一个顶层输出端口，属于集合 O
        - 如果输出端口驱动的输入端口数量大于等于 1，那么他是内部的连接
    - 一个输入端口由0到1个输出端口驱动
        - 如果一个输入端口没有被驱动，那么他是一个顶层输入端口，属于集合 I
        - 输入端口被多个输出端口驱动是非法的
- 模块之间端口的连接还满足以下约束
    - 属于 I 和 O 的端口只能是 corvus_comb_P* 模块的输入和输出
    - corvus_seq_Px 的输入只能来自 corvus_comb_Px 的输出
    - corvus_external 的输入端口集合记为 Ei，输出端口集合记为 Eo
        - Ei 只能来自 corvus_comb_P* 的输出
        - Eo 只能输出到 corvus_comb_P* 的输入

## 面向 corvus 的代码生成规则

- 连接关系分类
    - 顶层端口集合 I 和 O
    - external 模块的端口集合 Ei 和 Eo
    - 将 Ci 到 Si 的信号称为 localCtSi
    - 将 Si 到 Ci 的信号称为 localStCi
    - 将 Si 到 Cj (i!=j) 的信号称为 remoteSitCj
- 连接的数据类型：使用 verilator 的数据类型
- 数据传输方法
    - 端点与拓扑
        - I、O、Ei、Eo 端口通过 MBus 传输，在 sendIAndEOutput、loadOAndEInput、loadBusCInputs、sendCOutputsToBus 中处理，targetId=0 表示 Top，targetId=1..N 表示各 SimWorker
        - remoteSitCj 端口通过 SBus 传输，在 loadBusCInputs、sendSOutputs、loadSInputs 中处理，targetId 为目标 worker 的编号
        - localCtSi、localStCi 端口通过共享内存传输，在 loadSInputs、loadLocalCInputs 中处理，避免走总线
    - payload 编解码（MBus/SBus 通用，动态按方向/信号确定）
        - 步骤 1：确定 slotBits。按接收端独立划分地址空间：每个 Top/SimWorker 仅为自己要接收的信号建立 slotId 空间；发送端按目标接收端的 slotId 映射发送 payload。对每个接收端计算 slotBits=ceil(log2(slotCount))，再向上取整到 {8,16,32} 中的最小可行值并固定（MBus/SBus 只是承载通道，slot 空间以接收端为粒度）
        - 步骤 2：对每个 slot 判断是否需要分 chunk。先在不分 chunk 的假设下，取 dataBitsCandidate 为 {32,16,8} 中不超过 (48-slotBits) 的最大值；若 width<=dataBitsCandidate，则 chunkBits=0，dataBits=dataBitsCandidate
        - 步骤 3：需分 chunk 时，chunkBits 依次尝试 {8,16,32}，优先取最小可行值；在 chunkBits 确定的情况下，令 dataBits=48-slotBits-chunkBits，再向下选取 {32,16,8} 中不超过该空间的最大值；计算 chunkCount=ceil(width/dataBits)，若 chunkCount 超出 chunkBits 覆盖范围则提升 chunkBits（仍按 8/16/32 顺序）并重新选择 dataBits，直到收敛且 dataBits>0
        - 编码布局（高→低）：chunkIdx(chunkBits，可为 0)|data(dataBits)|slotId(slotBits)。slotId 固定放最低位以便首先解码，其次是数据，再上方是 chunkIdx；不再使用 single/last。未分 chunk 的信号 chunkBits=0、chunkIdx 省略/视作 0
    - MBus 路径（顶层与 worker 间）
        - sendIAndEOutput：TopModule 将 I/Eo 输出编码后按 round-robin 分发到 mBusEndpoints，targetId=目标 worker
        - loadBusCInputs：SimWorker 轮询所有 mBusEndpoints 的 bufferCnt，直到为空；每个 payload 解码 slotId+chunkIdx 拼装后写入 cModule 输入，按预先记录的 chunkCount 判断何时该 slot 数据收齐
        - sendCOutputsToBus：SimWorker 将 cModule 的输出中需要进入 MBus 的部分（包括 O/Ei 方向）编码后发送到 targetId=0，可在多个端点间交错发送以平衡吞吐
        - loadOAndEInput：TopModule 在 masterSyncFlag 后从所有 mBusEndpoints 读取，按 slotId 还原 O/Ei，检测到缺片或重复时记录错误或丢弃残片，确保下一周期前 buffer 已耗尽
    - SBus 路径（worker 间）
        - sendSOutputs：sModule->eval 后，对 remoteSitCj 编码并发送到 targetId=目标 worker，若存在多个 sBusEndpoints 采用 round-robin 或按 targetId 映射的策略分摊
        - loadSInputs：下一次 sModule 计算前，耗尽所有 sBusEndpoints 的 buffer，将收到的 payload 按 chunkIdx 写入 sModule 输入，按预先记录的 chunkCount 判断何时该 slot 数据收齐
    - 共享内存路径（同 worker 内）
        - localCtSi 在 loadSInputs 阶段直接从 cModule 输出拷贝到 sModule 输入
        - localStCi 在 loadLocalCInputs 阶段从 sModule 输出拷贝到 cModule 输入，为下一轮循环准备
    - 收发顺序与清理
        - MBus/SBus 保证不重不漏但不保证顺序，接收端必须基于 slotId+chunkIdx 重组；每轮计算前 bufferCnt 应为 0，reset 时或检测到脏数据时可调用 clearBuffer，正常发送流程不需要在发送前清理 buffer
        - 有多个 MBus 和 SBus 时，发送端尽量交错使用端点提升并行度；接收端必须遍历所有端点直到耗尽，避免遗漏
- 生成一个继承自 CorvusTopModule 的派生类 CorvusTopModuleGen
    - 顶层端口生成
        - 包含一个继承自 TopPorts 的内部类 TopPortsGen，表示顶层端口集合
            - TopPortsGen 包含 I 和 O 端口对应的成员变量
            - TopPortsGen 的成员变量使用和 Verilator 生成端口相同的数据类型
        - 重载 createTopPorts() = 0; 虚方法，创建 TopPortsGen 实例并返回指针
        - 重载 deleteTopPorts() = 0; 虚方法，删除 TopPortsGen 实例
    - 实例化 external 模块
        - 重载 createExternalModule() = 0; 虚方法，创建 external 模块实例并返回指针
        - 重载 deleteExternalModule() = 0; 虚方法，删除 external 模块实例
    - 重载 sendIAndEOutput()
        - 将 TopPortsGen 中的 I 通过 MBus 发送到各 SimWorker
        - 将 external 的所有输出 Eo 通过 MBus 发送到各 SimWorker
    - 重载 loadOAndEInput()
        - 从 MBus 接收各 SimWorker 发送的 O，写入 TopPorts
        - 从 MBus 接收各 SimWorker 发送的 Ei，写入 external 模块
    - 重载时注意指针的多态转换
- 生成一组继承自 CorvusSimWorker 的派生类 CorvusSimWorkerGenP0 .. CorvusSimWorkerGenPN-1，对于每个 Pi
    - 模块实例化
        - 重载 createSimModules() = 0; 虚方法，创建 corvus_comb_Pi 和 corvus_seq_Pi 实例并返回指针，挂载到 cModule/sModule
        - 重载 deleteSimModules() = 0; 虚方法，删除上述实例
    - 重载 loadBusCInputs()
        - 从 MBus 接收 TopModule 发送的 C 输入，设置 cModule 的输入
        - 从 SBus 接收其他 SimWorker 发送的 remoteSjtCi，设置 cModule 的输入
    - 重载 sendCOutputsToBus()
        - 将 cModule 的 C 输出中属于 O/Ei 方向的部分通过 MBus 发送到 TopModule
        - 将 cModule 的 C 输出中属于 remoteCiStj 方向的部分通过 SBus 发送到目标 SimWorker
    - 重载 loadSInputs()
        - Si 的 input 只来自本 worker 内部
        - 从 Ci 的 output 拷贝 localCtSi 到 Si 的 input
    - 重载 sendSOutputs()
        - 将 sModule 的 S 输出中属于 remoteSitCj 方向的部分通过 SBus 发送到目标 SimWorker
    - 重载 loadLocalCInputs()
        - 从 sModule 的 S 输出中拷贝 localStCi 到 cModule 的 C 输入
