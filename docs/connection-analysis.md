# Connection Analysis

`ConnectionBuilder::analyze` 对模块端口连接做 corvus 语义分类并返回 `ConnectionAnalysis` 数据结构，供生成器直接消费。

## 输出结构
- `top_inputs` / `top_outputs`：I/O 顶层端口；`driver.module` 为空表示顶层输入。
- `external_inputs` / `external_outputs`：Ei/Eo，方向分别为 comb→external、external→comb。
- `partitions[pid]`：
  - `local_cts_to_si`：comb_pid → seq_pid（同分区）
  - `local_stc_to_ci`：seq_pid → comb_pid（同分区）
  - `remote_s_to_c`：seq_pid → comb_pj（pid != pj）
- `warnings`：合法性或匹配退化的非致命提示。

每条 `ClassifiedConnection` 携带完整的 `port_name`、`width/width_type`，`SignalEndpoint` 指向对应的模块与端口。

## 分类规则概要
1. 先按端口名分组、比对位宽，得到原始 `PortConnection`（支持多 driver/receiver）。
2. slot 空间按接收端划分：只为接收的信号创建 slot，不为未消费的 driver 膨胀计数。
3. 对每条连接逐个 receiver 拆分分类：
   - 无 driver → `top_inputs`
   - 无 receiver → `top_outputs`
   - COMB driver：同分区 SEQ → `local_cts_to_si`；EXTERNAL → `external_inputs`；其他类型告警
   - SEQ driver：COMB 同分区 → `local_stc_to_ci`；COMB 跨分区 → `remote_s_to_c`；其他类型告警
   - EXTERNAL driver：COMB → `external_outputs`；其他类型告警

## 使用示例
```c++
std::vector<ModuleInfo> modules = /* parser 输出 */;
ConnectionBuilder builder;
ConnectionAnalysis graph = builder.analyze(modules);

// 例如：遍历分区内的本地 C->S 信号
for (const auto& kv : graph.partitions) {
  int pid = kv.first;
  for (const auto& conn : kv.second.local_cts_to_si) {
    // conn.port_name / conn.width / conn.driver / conn.receivers
  }
}
```
