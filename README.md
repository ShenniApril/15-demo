# 15-demo：自适应压缩策略优化

面向多类型数据对象，在存储容量、总压缩时间和单对象恢复时延约束下，选择不压缩、单阶段或有序双阶段方案，最小化恢复、压缩与空间的加权总代价。

## 内容

- `15.cpp`：小规模精确搜索、大规模归一化拉格朗日搜索、资源修复及局部改进。
- `15_baseline.cpp`：原始 demo，保留作对照。
- `evaluate.py`、`evaluate_stress.py`：固定种子模拟数据生成、独立验解、穷举/MILP 对照和性能评测。
- `evaluation/results.json`、`evaluation/stress_results.json`：实测结果记录。
- `15_sample.txt`：题面样例。

## 结果

主测试 59 个、额外压力测试 20 个均输出合法方案；小规模与穷举最优答案一致。中规模最优代价差距平均 0.153%，额外紧约束测试最大差距 5.80%。本机 10 万对象测试约 1.06–1.19 秒，峰值工作集约 206 MB。

模拟测试不能保证隐藏测试通过率或官方得分；启发式算法不保证任意输入的可行解或全局最优。

## 编译与复现

需要支持 `__int128` 的 GCC、Python 和 SciPy。

```powershell
g++ -O2 -std=c++17 -Wall -Wextra 15.cpp -o 15_solver.exe
g++ -O2 -std=c++17 15_baseline.cpp -o 15_baseline.exe
Get-Content .\15_sample.txt | .\15_solver.exe
python evaluate.py
python evaluate_stress.py
```

Linux/macOS 的样例运行方式为 `./15_solver.exe < 15_sample.txt`；评测脚本的启动与耗时统计可用，内存采样仅实现 Windows API。

详见 [可行性评测报告](15_可行性评测.md)、[复现说明](README_eval.md) 和 [原始分析](15.md)。大量可重新生成的模拟输入/输出、缓存和本地可执行文件不纳入仓库，原始题面图片保留。
