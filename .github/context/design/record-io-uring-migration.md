# Record 文件 IO 迁移与性能对比（apollo-lite `100-disk-write-optimization` -> core）

## 1. 目标与迁移边界

- 来源：`apollo-lite` 分支 `100-disk-write-optimization` 中 `cyber/record/file` 的 io_uring 读写思路。
- 迁移目标：
  - 将 `RecordFileReader` / `RecordFileWriter` 从阻塞 `read/write` 迁移为 io_uring 提交/完成模型。
  - 去掉 `O_DIRECT` 打开方式（避免 4K 对齐约束对业务写入路径的侵入）。
  - 保持 record 文件格式、索引语义与既有 `record_file_test` / `record_reader_test` / `record_viewer_test` 兼容。

## 2. 实现概览

### 2.1 Reader

- 使用注册缓冲池 + fixed read：
  - 启动时分配 `kSlotCount * kSlotSize`，并通过 `io_uring_register_buffers()` 注册。
  - 顺序读取使用 `io_uring_prep_read_fixed()` + 预读窗口（`kPrefetchDepth`）。
  - `ReadSection<T>` 优先直接在 Slot 内存上 `ArrayInputStream` 解析；仅跨 Slot 时落入复用缓冲。
  - 等待 completion 改为 `peek + Yield` 轮询，避免 `wait_cqe` 长阻塞。

### 2.2 Writer

- 写路径重构为生产者/消费者流水线：
  - **Producer**：`WriteMessage` 仅做入队，先从 `SlotPool` 申请槽位、填充 `SingleMessage`，再推入有界 `Bounded MPSC Ring`。
  - **Backpressure**：队列满时执行 **Drop & Alert**（丢弃当前帧并累计计数），按固定阈值限频告警 `Record queue overflow`，不阻塞生产者线程。
  - **Backend Writer**：独立单线程从队列批量消费，聚合为 `Chunk`，并统一执行磁盘落盘。
  - **io_uring writev**：Section 使用 `io_uring_prep_writev()` 提交（`Section` 头与 payload 分离 iovec），减少拼接拷贝；提交采用批次刷入并通过 completion 回收请求对象。
  - **Slot return**：消息一旦进入 backend 的 `Chunk`，对应槽位立即归还 `SlotPool`，维持运行期零动态节点分配。

### 2.3 O_DIRECT 调整

- 打开文件使用：`O_CREAT | O_WRONLY | O_TRUNC`（不再使用 `O_DIRECT`）。
- 去掉对齐填充前提，序列化 payload 按逻辑长度直接提交。

### 2.4 依赖

- 按要求在 `MODULE.bazel` 引入：
  - `bazel_dep(name = "liburing", version = "2.14.bcr.1")`
- `.bazelrc` 增加官方 BCR registry：
  - `build --registry=https://bcr.bazel.build`
  以确保 `liburing@2.14.bcr.1` 可解析。

## 3. 性能测试设计

新增目标：`//cyber/record:record_io_perf`（`cyber/record/file/record_io_perf.cc`）

- 负载模型：
  - 写入 30000 条消息，每条 payload 4096 bytes（总 payload 122,880,000 bytes）。
  - 同进程随后读取并完整解析 chunk body。
- 采集指标：
  - wall time、吞吐（MiB/s）
  - user/sys CPU 时间（`getrusage`）
  - 峰值内存 `max_rss_kb`
  - 进程 I/O 统计（`/proc/self/io` 的 `rchar/wchar/read_bytes/write_bytes`）
- 每组执行 3 次，取平均。

命令：

```bash
bazel build //cyber/record:record_io_perf
./bazel-bin/cyber/record/record_io_perf 30000 4096
```

## 4. baseline 与优化后对比（30000 * 4096，3 次平均）

### 4.1 初始迁移版本（退化）

| 指标 | 阻塞版（修改前） | 初始 io_uring 版 | 变化 |
|---|---:|---:|---:|
| 写入 wall (ms) | 157.139 | 185.082 | +17.78% |
| 写入吞吐 (MiB/s) | 746.041 | 633.169 | -15.13% |
| 写入 user CPU (ms) | 46.597 | 41.901 | -10.08% |
| 写入 sys CPU (ms) | 35.249 | 66.679 | +89.16% |
| 写入 max RSS (KB) | 16693.333 | 137386.667 | +723.00% |
| 读取 wall (ms) | 38.052 | 47.833 | +25.70% |
| 读取吞吐 (MiB/s) | 3079.693 | 2455.027 | -20.28% |
| 读取 user CPU (ms) | 20.588 | 32.993 | +60.26% |
| 读取 sys CPU (ms) | 17.643 | 14.850 | -15.83% |
| 读取 max RSS (KB) | 16693.333 | 137386.667 | +723.00% |

### 4.2 本轮最终优化版本（旧批次）

| 指标 | 初始 io_uring 版 | 最终优化版 | 变化 |
|---|---:|---:|---:|
| 写入 wall (ms) | 160.674 | 60.400 | -62.41% |
| 写入吞吐 (MiB/s) | 729.359 | 1951.473 | +167.56% |
| 写入 user CPU (ms) | 47.594 | 44.332 | -6.85% |
| 写入 sys CPU (ms) | 75.219 | 41.461 | -44.88% |
| 写入 max RSS (KB) | 137198.667 | 37410.667 | -72.73% |
| 读取 wall (ms) | 50.026 | 45.520 | -9.01% |
| 读取吞吐 (MiB/s) | 2343.150 | 2574.983 | +9.89% |
| 读取 user CPU (ms) | 35.640 | 30.583 | -14.19% |
| 读取 sys CPU (ms) | 14.395 | 14.952 | +3.87% |
| 读取 max RSS (KB) | 137198.667 | 41194.667 | -69.97% |

### 4.3 与阻塞版（最初实现）对比

| 指标 | 阻塞版（修改前） | 最终优化版 | 变化 |
|---|---:|---:|---:|
| 写入 wall (ms) | 157.139 | 60.400 | -61.56% |
| 写入吞吐 (MiB/s) | 746.041 | 1951.473 | +161.58% |
| 写入 max RSS (KB) | 16693.333 | 37410.667 | +124.11% |
| 读取 wall (ms) | 38.052 | 45.520 | +19.63% |
| 读取吞吐 (MiB/s) | 3079.693 | 2574.983 | -16.39% |
| 读取 max RSS (KB) | 16693.333 | 41194.667 | +146.77% |

## 5. 结论

- 已完成从阻塞读写到 io_uring API 形态的迁移，并去除了 `O_DIRECT`。
- 依赖侧已切换为纯 `bazel_dep(name = "liburing", version = "2.14.bcr.1")`（非本地兼容模块）。
- 最终优化版相较初始 io_uring 版本已显著恢复并提升写路径吞吐，CPU 与内存开销大幅回落。
- 当前瓶颈仍在读取吞吐（仍低于最初阻塞版），下一步应聚焦 chunk-body 反序列化成本与更深 readahead 窗口调优。

## 6. 真实 Record（`/mnt/synology/apollo/sensor_rgb.record`）在线回放分析

### 6.1 新增真实基准程序

- 新增 `//cyber/record:record_perf_reader`（`cyber/record/file/record_perf_reader.cc`）
- 支持模式：
  - `--mode=baseline`：同步 `pread` + `ChunkBody` 整块解析（接近原始路径）
  - `--mode=uring`：io_uring Ping-Pong + `ChunkBody` 整块解析
  - `--mode=uring_stream`：io_uring Ping-Pong + `SingleMessage` 流式复用解析（在线回放优化路径）
  - `--mode=uring_hysteresis`：ReaderPump + ReservoirQueue + HWM/LWM 滞回控制 + Publisher 节拍发布
- 测试文件信息：
  - `sensor_rgb.record` 大小 `7,626,958,089` bytes（约 7.2 GiB）
  - Chunk 数 `37`
  - 最大 Chunk `215,021,227` bytes（约 205 MiB）

### 6.2 真实数据结果（全文件）

| 指标 | baseline | uring_stream | uring_hysteresis |
|---|---:|---:|---:|
| wall (ms) | 72733.0 | 14504.0 | 3667.6 |
| 吞吐 (MiB/s) | 100.003 | 501.483 | 1983.200 |
| user CPU (ms) | 1088.52 | 1011.85 | 1241.21 |
| sys CPU (ms) | 9835.24 | 2306.01 | 1280.84 |
| max RSS (KB) | 3912372 | 441768 | 451096 |
| jitter stddev (us) | 0.021 | 0.000 | 4202.970 |
| pause_count | 0 | 0 | 0 |
| queue_peak_bytes | - | - | 9271660 |

备注：全文件回放中队列峰值仅 9.2MB，低于默认 HWM=500MB，因此未触发 pause；`uring_hysteresis` 的背压控制在该配置下处于“可用但未激活”状态。

### 6.2.1 背压触发验证（10 chunks, HWM=50MB, LWM=10MB, replay_rate=5）

| 指标 | baseline | uring_stream | uring_hysteresis |
|---|---:|---:|---:|
| wall (ms) | 1359.2 | 340.8 | 3458.7 |
| 吞吐 (MiB/s) | 1482.1 | 5910.6 | 582.4 |
| sys CPU (ms) | 1149.1 | 323.4 | 480.2 |
| max RSS (KB) | 1768248 | 442080 | 505268 |
| pause_count | 0 | 0 | 35 |
| pause_duration (ms) | 0 | 0 | 2373.2 |
| queue_peak_bytes | - | - | 55945250 |

结论：在慢发布 + 小水位下，滞回背压正常触发并抑制队列失控（峰值约 55.9MB，接近 HWM）。

### 6.3 瓶颈证据（strace）

- baseline：
  - 主调用为 `pread64`（82 次）
  - 总 syscall `1651`
- uring_stream：
  - 主调用为 `io_uring_enter`（73 次）
  - 总 syscall `214`

=> 系统调用总次数约下降 **87%**，内核态时间（sys CPU）显著下降。

### 6.4 闭环调试场景结论

- 在线回放与闭环调试建议采用 `uring_stream` 路径：
  - 单线程顺序消费，保持时序可控；
  - Ping-Pong 预读隐藏 IO 等待；
  - 流式复用解析避免 `ChunkBody` 整块构造，显著降低 RSS 与 sys CPU。
- 当需要“读取与发布解耦 + 内存硬上限”时启用 `uring_hysteresis`：
  - ReaderPump 高速灌队列；
  - Publisher 按时钟匀速放水；
  - HWM/LWM 滞回确保不会在阈值点抖动。
- 若回放严格要求极低微抖动，应以 `uring_hysteresis` 的发布节拍为准，并根据业务调大 `replay_rate`/放宽水位，平衡抖动与吞吐。

### 6.5 复现实验命令

```bash
bazel build //cyber/record:record_perf_reader
./bazel-bin/cyber/record/record_perf_reader --mode=baseline --record=/mnt/synology/apollo/sensor_rgb.record
./bazel-bin/cyber/record/record_perf_reader --mode=uring --record=/mnt/synology/apollo/sensor_rgb.record
./bazel-bin/cyber/record/record_perf_reader --mode=uring_stream --record=/mnt/synology/apollo/sensor_rgb.record
./bazel-bin/cyber/record/record_perf_reader --mode=uring_hysteresis --record=/mnt/synology/apollo/sensor_rgb.record --hwm_mb=500 --lwm_mb=100 --replay_rate=200

# 一键跑对比 + strace 汇总
bash scripts/record_perf_reader.sh /mnt/synology/apollo/sensor_rgb.record 0
```

说明：
- 当前环境缺少 `pidstat`，使用程序内 `getrusage` 与 `/proc` 采样替代。
- 当前环境 `perf_event_paranoid=4`，无法直接采集 perf 火焰图（需管理员调低后再采样）。

## 7. 2026-06-27 写入侧重构（MPSC + writev）结果

### 7.1 本次重构点

- `record_file_writer` 已切换为：
  - `SlotPool(65536)`：预分配消息槽位
  - `Bounded MPSC Ring`：多生产者单消费者有界队列
  - Drop & Alert：队列满时丢弃并按 100 帧限频告警
  - Backend 线程：单线程聚合 Chunk 并执行 `io_uring_prep_writev` 落盘

### 7.2 回归测试

```bash
bazel test \
  //cyber/record:record_file_test \
  //cyber/record:record_reader_test \
  //cyber/record:record_viewer_test \
  --test_output=errors
```

结果：3/3 通过。

### 7.3 合成写读基准（本次代码）

```bash
bazel run //cyber/record:record_io_perf -- 50000 4096
```

结果（单次）：

| 指标 | 数值 |
|---|---:|
| 写入 wall (ms) | 107.269 |
| 写入吞吐 (MiB/s) | 1820.77 |
| 写入 user/sys CPU (ms) | 113.451 / 107.284 |
| 写入 max RSS (KB) | 183236 |
| 读取 wall (ms) | 71.857 |
| 读取吞吐 (MiB/s) | 2718.07 |

### 7.4 真实 record 复测（本次代码）

测试文件：`/mnt/synology/apollo/sensor_rgb.record`

| 模式 | wall (ms) | 吞吐 (MiB/s) | user/sys CPU (ms) | max RSS (KB) |
|---|---:|---:|---:|---:|
| baseline | 4912.62 | 1480.58 | 947.258 / 3945.67 | 3912756 |
| uring_stream | 1215.68 | 5983.10 | 951.758 / 1199.14 | 442080 |
| uring_hysteresis (HWM=256MB, LWM=64MB, replay_rate=300) | 2242.22 | 3243.90 | 1219.09 / 1095.72 | 451088 |

## 8. 2026-06-27 写入侧验证经验（无收益方案已回退）

本轮按“无收益不合入”执行了一个组合实验，并已确认回退：

- 实验内容（组合）：
  - 将 fixed buffer 路径改为把 `SECTION_CHUNK_BODY` 头和 body 合并到同一固定缓冲，尝试单次 `io_uring_prep_write_fixed`。
  - 同时放宽批次阈值（增大 `max_batch_msgs` / `max_batch_bytes`）。
- 合成基准（`record_io_perf 30000 4096`，5 次均值）：
  - 实验前：`write wall=40.95 ms`，`throughput=2865.82 MiB/s`，`sys=54.03 ms`
  - 实验后：`write wall=48.17 ms`，`throughput=2464.79 MiB/s`，`sys=66.22 ms`
- 结论：
  - 该组合策略在当前机器与负载下显著退化（wall 变长、吞吐下降、sys 升高），不满足优化目标。
  - 代码已回退，不纳入主实现。

补充观测（`strace -f -c`）：
- 写路径主要系统调用耗时集中在 `io_uring_enter` 与线程同步（`futex`）。
- 后续优化应优先减少无效提交/等待循环与线程唤醒抖动，再评估更激进的 fixed-buffer 编排。

## 9. 2026-06-27 写入侧验证经验（二）：通知门控无收益，已回退

- 实验内容：
  - 尝试将 producer 唤醒策略从“每次入队后 `notify_one`”改为“仅队列从空变非空时 `notify_one`”，以减少 `futex` 抖动。
- 合成基准（`record_io_perf 30000 4096`，8 次均值）：
  - 实验前：`write wall=42.33 ms`，`throughput=2770.90 MiB/s`，`sys=57.52 ms`
  - 实验后：`write wall=44.65 ms`，`throughput=2640.67 MiB/s`，`sys=61.77 ms`
- 结论：
  - 该门控策略在当前实现下导致写入性能和 sys CPU 同时退化，不采纳。
  - 代码已回退，保留“每次入队后 notify”策略。

## 10. 2026-06-27 写入侧验证经验（三）：提高 submit 批次无显著收益，已回退

- 实验内容：
  - 将 `kSubmitBatch` 从 `16` 提升到 `64`，目标是减少 `io_uring_enter` 频率并降低 sys CPU。
- 合成基准（`record_io_perf 30000 4096`，8 次均值）：
  - 调整前：`write wall=41.75 ms`，`throughput=2818.55 MiB/s`，`sys=55.04 ms`
  - 调整后：`write wall=41.56 ms`，`throughput=2829.23 MiB/s`，`sys=54.86 ms`
- 结论：
  - 指标变化幅度极小，处于噪声区间，未形成可复现的显著收益。
  - 该参数不做合入，代码回退为 `kSubmitBatch=16`。

## 11. 2026-06-27 写入侧验证经验（四）：放大每 chunk 消息数导致退化，已回退

- 实验内容（单变量）：
  - 仅将 `kMaxBatchMsgs` 从 `64` 提高到 `512`（其余参数不变）。
- 合成基准（`record_io_perf 30000 4096`，12 次均值）：
  - 调整前：`write wall=42.73 ms`，`throughput=2751.74 MiB/s`，`sys=57.43 ms`
  - 调整后：`write wall=65.52 ms`，`throughput=2239.22 MiB/s`，`sys=68.78 ms`
- 结论：
  - 该调整显著退化（wall 变长、吞吐下降、sys 升高），不采纳。
  - 原因分析：在当前实现下，过大的消息批次会放大 backend 单轮处理时长与尾部等待，导致 producer/backend 协同抖动增加，整体反而变慢。
  - 代码已回退为 `kMaxBatchMsgs=64`。

## 12. 2026-06-27 写入侧验证经验（五）：关闭 fixed chunk buffer 路径退化，已回退

- 实验内容（单变量）：
  - 仅将 `kChunkBufferCount` 从 `8` 调整到 `0`（禁用 fixed chunk buffer 路径）。
- 合成基准（`record_io_perf 30000 4096`，12 次均值）：
  - 调整前：`write wall=42.66 ms`，`throughput=2755.94 MiB/s`，`sys=57.59 ms`
  - 调整后：`write wall=46.78 ms`，`throughput=2534.13 MiB/s`，`sys=59.62 ms`
- 结论：
  - 关闭 fixed buffer 会同步拉低吞吐并抬高 sys，不采纳。
  - 代码已回退为 `kChunkBufferCount=8`。

## 13. 2026-06-27 写入侧验证经验（六）：降低 completion polling 频率退化，已回退

- 实验内容（单变量）：
  - backend completion 轮询从“每消息 `PollCompletions(false)`”改为“每 32 条或 inflight 达上限再轮询”。
- 合成基准（`record_io_perf 30000 4096`，12 次均值）：
  - 调整前：`write wall=42.66 ms`，`throughput=2755.94 MiB/s`，`sys=57.59 ms`
  - 调整后：`write wall=59.33 ms`，`throughput=2553.76 MiB/s`，`sys=59.88 ms`
- 结论：
  - 该策略引入明显长尾与整体退化，不采纳。
  - `strace -f -c` 显示 `io_uring_enter` 调用量未显著下降（约 132~135 次量级），说明瓶颈不在这类轮询降频。
  - 代码已回退为“每消息 polling”的现有策略。

## 14. 阶段性主要矛盾归纳（用于下一轮实验设计）

- 已排除或验证无收益项：
  - submit 批次放大（16->64）无显著收益（噪声区间）。
  - notify 门控策略（空->非空）退化。
  - 放大 `kMaxBatchMsgs`（64->512）退化。
  - 禁用 fixed chunk buffer 退化。
  - completion polling 降频退化。
- 当前主要矛盾更接近：
  - 写入路径消息级管理开销（slot 入池/出池、chunk 聚合、索引更新）在高频小包场景占比高；
  - 而不是单一的 `io_uring_enter` 调用次数问题。
- 下一轮应优先验证“减少消息级管理开销”的方案（保持单变量与可回退）：
  - 例如固定 payload slab 与 slot 生命周期优化、索引/元信息更新合并、以及 writer 线程内批处理路径的结构性简化。

## 15. 本次读写重构最终结论（2026-06-27）

- 读取侧（真实 record）：
  - `uring_stream` 相比 `baseline` 在 wall/吞吐/sys/RSS 上均明显更优，适合作为默认高吞吐回放路径。
  - `uring_hysteresis` 在慢发布和小水位下可稳定触发背压，适合需要“读取-发布解耦 + 内存上限”的场景。
- 写入侧（合成 30000x4096）：
  - `SlotPool + Bounded MPSC + Backend Writer + io_uring(writev/fixed)` 架构已稳定，功能与回归测试通过。
  - 多轮单变量实验已排除多项“看起来合理但无收益/退化”的调优项，当前版本保留了收益更稳定的实现。
  - 当前主要矛盾仍是消息级管理开销，而非单一 `io_uring_enter` 次数。

## 16. baseline 测试与对比方法（建议流程）

### 16.1 测试前准备

1. 使用同一台机器、同一内核与磁盘环境。
2. 固定输入数据与参数，避免混入缓存/并发扰动。
3. 每个候选改动仅做单变量实验，不做组合调参。

### 16.2 合成 baseline（写+读）

命令：

```bash
bazel build //cyber/record:record_io_perf
./bazel-bin/cyber/record/record_io_perf 30000 4096
```

建议批量统计（至少 12 次）：

```bash
python3 - <<'PY'
import json, subprocess, statistics
runs = 12
ws, wt, wsys, wrss = [], [], [], []
for _ in range(runs):
    p = subprocess.run(
        ["./bazel-bin/cyber/record/record_io_perf", "30000", "4096"],
        capture_output=True, text=True, check=True)
    w = json.loads(p.stdout)["write"]
    ws.append(w["wall_ms"]); wt.append(w["throughput_mib_s"])
    wsys.append(w["sys_cpu_ms"]); wrss.append(w["max_rss_kb"])
print({
  "runs": runs,
  "write_wall_avg_ms": sum(ws)/runs,
  "write_wall_median_ms": statistics.median(ws),
  "write_tp_avg_mib_s": sum(wt)/runs,
  "write_sys_avg_ms": sum(wsys)/runs,
  "write_sys_median_ms": statistics.median(wsys),
  "write_rss_min_kb": min(wrss),
  "write_rss_max_kb": max(wrss),
})
PY
```

### 16.3 真实 record baseline（读取）

命令：

```bash
bazel build //cyber/record:record_perf_reader
./bazel-bin/cyber/record/record_perf_reader \
  --file=/mnt/synology/apollo/sensor_rgb.record --mode=baseline
./bazel-bin/cyber/record/record_perf_reader \
  --file=/mnt/synology/apollo/sensor_rgb.record --mode=uring_stream
```

若要验证背压模型：

```bash
./bazel-bin/cyber/record/record_perf_reader \
  --file=/mnt/synology/apollo/sensor_rgb.record \
  --mode=uring_hysteresis --hwm_mb=50 --lwm_mb=10 --replay_rate=5 --max_chunks=10
```

### 16.4 判定与合入标准

- 必须满足：`sys_cpu_ms` 明显下降（建议阈值 ≥10%），且吞吐不下降、wall 不恶化。
- 若改动效果在噪声区间（小幅涨跌、无法复现），视为无收益，不合入。
- 若改动在任一核心指标上显著退化，立即回退，并把实验数据与结论记录到本文件。
