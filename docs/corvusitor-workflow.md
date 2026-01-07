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


## 面向多线程的代码生成规则
