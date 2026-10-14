# 第 15 题：求解与模拟评测

结论和实测结果见 [可行性评测报告](15_可行性评测.md)。当前程序是可运行的近似求解版本，不保证所有输入的全局最优，也不保证每个合法大规模输入都找到可行解。

## 文件

- `15.cpp` / `15_solver.exe`：改进后的求解器，输出保持题面格式。
- `15_baseline.cpp` / `15_baseline.exe`：原 demo 的对照版本。
- `evaluate.py`：固定种子生成数据、独立验解、穷举及 SciPy MILP 对照、时间和内存评测。
- `evaluate_stress.py`：额外生成无典型算法特征、双资源紧约束的数据，用 MILP 检查质量。
- `evaluation/results.json` / `evaluation/stress_results.json`：完整结果。
- `evaluation/*.txt`：生成的单用例输入；相应 `.out` / `.err` 是结果和诊断。

## 复现

在本目录的 PowerShell 中运行（需要支持 `__int128` 的 GCC 和含 SciPy 的 Python）：

```powershell
g++ -O2 -std=c++17 -Wall -Wextra 15.cpp -o 15_solver.exe
g++ -O2 -std=c++17 15_baseline.cpp -o 15_baseline.exe
Get-Content .\15_sample.txt | .\15_solver.exe
python evaluate.py
python evaluate_stress.py
```

环境实测：Windows，MinGW GCC 8.1.0，SciPy 1.15.2。耗时包含程序启动、输入解析、求解和输出，不包含 Python 生成数据与参考求解器；内存使用 Windows `GetProcessMemoryInfo` 采样读取的 `PeakWorkingSetSize`，不包含评测器本身。CPU 型号未能读取。主评测按顺序运行，原 demo 超过 3 秒就终止；超过这一阈值只记录超时，不推算完整运行时间。SciPy 单用例上限 20 秒，只有成功证明最优的结果才用来计算最优差距。

## 生成原则

六类数据对象随机混合。A/B/C 的单阶段模拟值包含噪声和类型差异，图像类模拟已经压缩的数据；六种有序双阶段方案逐一产生实际阶段数值，不能通过单阶段压缩比相乘来推导。部分 C/F 用例恢复时延严格。先随机选择合法方案作为见证，再根据见证设置容量、时间预算，确保输入有可行解。普通组给 3% 资源余量，F 组按见证总量设置边界。见证不传给求解器。

这些只是人工设计的分布，不代表官方公开/隐藏数据，也没有模拟真实压缩算法的物理耗时。用例组 A–F 是本地对题面侧重点的覆盖，不是官方测试的复制。额外压力集独立随机生成候选大小、压缩时间和恢复时间，以降低仅在典型 A/B/C 数据上测试的偏差。
