#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using i64 = long long;
using i128 = __int128_t;

struct Stage {
    int algorithmId = 0;
    i64 inputBytes = 0;
    i64 outputBytes = 0;
    i64 compressTimeNs = 0;
    i64 decompressTimeNs = 0;
};

struct Option {
    int optionId = 0;
    std::vector<Stage> stages;
    i64 finalBytes = 0;
    i128 compressTimeNs = 0;
    i128 decompressTimeNs = 0;
    i128 cost = 0;
    double normalizedCost = 0, normalizedBytes = 0, normalizedTime = 0;
    bool valid = true;
};

struct DataObject {
    int objectId = 0;
    int dataType = 0;
    i64 originalBytes = 0;
    i64 accessFrequency = 0;
    i64 priority = 0;
    i64 maxRestoreLatencyNs = 0;
    std::vector<Option> options;
};

struct CaseData {
    i64 capacityBytes = 0;
    i64 compressBudgetNs = 0;
    i64 wr = 0;
    i64 wc = 0;
    i64 ws = 0;
    std::vector<DataObject> objects;
};

struct Solution {
    bool feasible = false;
    std::vector<int> optionIndexes;
    i128 totalBytes = 0;
    i128 totalCompressTimeNs = 0;
    i128 totalDecompressTimeNs = 0;
    i128 totalCost = 0;
};

namespace {

class FastInput {
    char buffer[1 << 16];
    std::size_t pos = 0, len = 0;
    int get() {
        if (pos == len) {
            len = std::fread(buffer, 1, sizeof(buffer), stdin);
            pos = 0;
            if (!len) return EOF;
        }
        return static_cast<unsigned char>(buffer[pos++]);
    }
public:
    template<class T> bool read(T& value) {
        int c;
        do { c = get(); } while (c != EOF && c <= ' ');
        bool negative = c == '-';
        if (negative) c = get();
        if (c < '0' || c > '9') return false;
        std::uint64_t bound = static_cast<std::uint64_t>(std::numeric_limits<T>::max()) + negative;
        std::uint64_t n = 0;
        do {
            unsigned d = static_cast<unsigned>(c - '0');
            if (n > (bound - d) / 10) return false;
            n = n * 10 + d;
            c = get();
        } while (c >= '0' && c <= '9');
        if (c != EOF && c > ' ') return false;
        if (negative && n == bound) value = std::numeric_limits<T>::min();
        else value = negative ? -static_cast<T>(n) : static_cast<T>(n);
        return true;
    }
    template<class T, class... Rest> bool read(T& value, Rest&... rest) {
        return read(value) && read(rest...);
    }
};
FastInput input;

std::string toString(i128 value) {
    if (value == 0) return "0";
    bool negative = value < 0;
    if (negative) value = -value;
    std::string result;
    while (value > 0) {
        result.push_back(static_cast<char>('0' + value % 10));
        value /= 10;
    }
    if (negative) result.push_back('-');
    std::reverse(result.begin(), result.end());
    return result;
}

bool better(const Solution& lhs, const Solution& rhs,
            const std::vector<DataObject>& objects) {
    if (!lhs.feasible) return false;
    if (!rhs.feasible) return true;
    if (lhs.totalCost != rhs.totalCost) return lhs.totalCost < rhs.totalCost;
    if (lhs.totalBytes != rhs.totalBytes) return lhs.totalBytes < rhs.totalBytes;
    if (lhs.totalCompressTimeNs != rhs.totalCompressTimeNs)
        return lhs.totalCompressTimeNs < rhs.totalCompressTimeNs;
    if (lhs.totalDecompressTimeNs != rhs.totalDecompressTimeNs)
        return lhs.totalDecompressTimeNs < rhs.totalDecompressTimeNs;
    for (std::size_t i = 0; i < lhs.optionIndexes.size(); ++i) {
        const int lhsId = objects[i].options[lhs.optionIndexes[i]].optionId;
        const int rhsId = objects[i].options[rhs.optionIndexes[i]].optionId;
        if (lhsId != rhsId) return lhsId < rhsId;
    }
    return false;
}

void evaluateChoice(const CaseData& data, const std::vector<int>& choice,
                    Solution& result) {
    result = {};
    result.optionIndexes = choice;
    for (std::size_t i = 0; i < choice.size(); ++i) {
        if (choice[i] < 0) return;
        const Option& option = data.objects[i].options[choice[i]];
        if (!option.valid) return;
        result.totalBytes += option.finalBytes;
        result.totalCompressTimeNs += option.compressTimeNs;
        result.totalDecompressTimeNs += option.decompressTimeNs;
        result.totalCost += option.cost;
    }
    result.feasible = result.totalBytes <= data.capacityBytes &&
                      result.totalCompressTimeNs <= data.compressBudgetNs;
}

// DFS try all the possible combinations of options for each object.
void exactDfs(const CaseData& data, std::size_t index, i128 usedBytes,
              i128 usedTime, i128 usedRestore, i128 cost,
              std::vector<int>& choice, Solution& best) {
    if (usedBytes > data.capacityBytes || usedTime > data.compressBudgetNs)
        return;
    if (index == data.objects.size()) {
        Solution candidate;
        candidate.feasible = true;
        candidate.optionIndexes = choice;
        candidate.totalBytes = usedBytes;
        candidate.totalCompressTimeNs = usedTime;
        candidate.totalDecompressTimeNs = usedRestore;
        candidate.totalCost = cost;
        if (better(candidate, best, data.objects)) best = std::move(candidate);
        return;
    }

    const DataObject& object = data.objects[index];
    for (std::size_t j = 0; j < object.options.size(); ++j) {
        const Option& option = object.options[j];
        if (!option.valid) continue;
        choice[index] = static_cast<int>(j);
        exactDfs(data, index + 1, usedBytes + option.finalBytes,
                 usedTime + option.compressTimeNs,
                 usedRestore + option.decompressTimeNs, cost + option.cost,
                 choice, best);
    }
}

// Normalized dual search, resource repair, then feasible local descent.
// Heuristic: failure to find a solution is not a proof of infeasibility.
Solution lagrangianHeuristic(CaseData& data) {
    Solution best;
    const long double bs = std::max<i64>(1, data.capacityBytes);
    const long double ts = std::max<i64>(1, data.compressBudgetNs);
    long double cs = 1;
    for (const auto& obj : data.objects) {
        i128 m = 0;
        for (const auto& o : obj.options) if (o.valid) m = std::max(m, o.cost);
        cs += static_cast<long double>(m);
    }
    for (auto& obj : data.objects) for (auto& o : obj.options) {
        o.normalizedCost = static_cast<double>(static_cast<long double>(o.cost) / cs);
        o.normalizedBytes = static_cast<double>(o.finalBytes / bs);
        o.normalizedTime = static_cast<double>(o.compressTimeNs / ts);
    }
    auto violation = [&](i128 b, i128 t) {
        return std::max(0.0L, (b - data.capacityBytes) / bs) +
               std::max(0.0L, (t - data.compressBudgetNs) / ts);
    };
    auto repair = [&](Solution s) {
        if (s.optionIndexes.size() != data.objects.size()) return s;
        // Each accepted move strictly decreases total normalized violation.
        // Cap full scans for predictable large-case running time.
        for (int pass = 0; pass < 6 && !s.feasible; ++pass) {
            bool moved = false;
            for (std::size_t i = 0; i < data.objects.size(); ++i) {
                int old = s.optionIndexes[i], pick = old;
                const auto& a = data.objects[i].options[old];
                long double v = violation(s.totalBytes, s.totalCompressTimeNs);
                long double score = std::numeric_limits<long double>::infinity();
                for (std::size_t j = 0; j < data.objects[i].options.size(); ++j) {
                    const auto& b = data.objects[i].options[j];
                    if (!b.valid || static_cast<int>(j) == old) continue;
                    long double nv = violation(s.totalBytes - a.finalBytes + b.finalBytes,
                        s.totalCompressTimeNs - a.compressTimeNs + b.compressTimeNs);
                    if (nv >= v - 1e-18L) continue;
                    long double q = static_cast<long double>(b.cost - a.cost) / cs / (v - nv);
                    if (q < score) { score = q; pick = static_cast<int>(j); }
                }
                if (pick == old) continue;
                const auto& b = data.objects[i].options[pick];
                s.optionIndexes[i] = pick;
                s.totalBytes += b.finalBytes - a.finalBytes;
                s.totalCompressTimeNs += b.compressTimeNs - a.compressTimeNs;
                s.totalDecompressTimeNs += b.decompressTimeNs - a.decompressTimeNs;
                s.totalCost += b.cost - a.cost;
                s.feasible = s.totalBytes <= data.capacityBytes &&
                    s.totalCompressTimeNs <= data.compressBudgetNs;
                moved = true;
                if (s.feasible) break;
            }
            if (!moved) break;
        }
        return s;
    };
    double lb = 0, lt = 0;
    long double closestV = std::numeric_limits<long double>::infinity();
    Solution closest;
    const int iterations = data.objects.size() > 10000 ? 24 : 96;
    for (int step = 0; step < iterations; ++step) {
        std::vector<int> choice(data.objects.size(), -1);
        for (std::size_t i = 0; i < data.objects.size(); ++i) {
            double score = std::numeric_limits<double>::infinity();
            for (std::size_t j = 0; j < data.objects[i].options.size(); ++j) {
                const auto& o = data.objects[i].options[j];
                if (!o.valid) continue;
                double q = o.normalizedCost + lb * o.normalizedBytes + lt * o.normalizedTime;
                int prev = choice[i];
                bool tie = prev >= 0 && q == score &&
                    std::make_pair(o.finalBytes, o.optionId) <
                    std::make_pair(data.objects[i].options[prev].finalBytes,
                                   data.objects[i].options[prev].optionId);
                if (q < score || tie) { score = q; choice[i] = static_cast<int>(j); }
            }
        }
        Solution s;
        evaluateChoice(data, choice, s);
        long double v = violation(s.totalBytes, s.totalCompressTimeNs);
        if (v < closestV) { closestV = v; closest = s; }
        if (better(s, best, data.objects)) best = s;
        if (step % 24 == 0) {
            auto r = repair(s);
            if (better(r, best, data.objects)) best = std::move(r);
        }
        double rate = 2.0 / std::sqrt(static_cast<double>(step + 1));
        lb = std::max(0.0, lb + rate * static_cast<double>((s.totalBytes - data.capacityBytes) / bs));
        lt = std::max(0.0, lt + rate * static_cast<double>((s.totalCompressTimeNs - data.compressBudgetNs) / ts));
    }
    auto r = repair(closest);
    if (better(r, best, data.objects)) best = std::move(r);
    if (!best.feasible) return best;
    for (int pass = 0; pass < 4; ++pass) {
        bool moved = false;
        for (std::size_t i = 0; i < data.objects.size(); ++i) {
            int pick = best.optionIndexes[i];
            const auto& a = data.objects[i].options[pick];
            for (std::size_t j = 0; j < data.objects[i].options.size(); ++j) {
                const auto& b = data.objects[i].options[j];
                const auto& p = data.objects[i].options[pick];
                if (!b.valid || best.totalBytes - a.finalBytes + b.finalBytes > data.capacityBytes ||
                    best.totalCompressTimeNs - a.compressTimeNs + b.compressTimeNs > data.compressBudgetNs) continue;
                if (b.cost < p.cost || (b.cost == p.cost &&
                    std::make_pair(std::make_pair(b.finalBytes, b.compressTimeNs),
                                   std::make_pair(b.decompressTimeNs, b.optionId)) <
                    std::make_pair(std::make_pair(p.finalBytes, p.compressTimeNs),
                                   std::make_pair(p.decompressTimeNs, p.optionId)))) pick = static_cast<int>(j);
            }
            if (pick == best.optionIndexes[i]) continue;
            const auto& b = data.objects[i].options[pick];
            best.totalBytes += b.finalBytes - a.finalBytes;
            best.totalCompressTimeNs += b.compressTimeNs - a.compressTimeNs;
            best.totalDecompressTimeNs += b.decompressTimeNs - a.decompressTimeNs;
            best.totalCost += b.cost - a.cost;
            best.optionIndexes[i] = pick;
            moved = true;
        }
        if (!moved) break;
    }
    return best;
}

Solution solve(CaseData& data) {
    std::uint64_t combinations = 1;
    constexpr std::uint64_t exactLimit = 5'000'000;
    for (const DataObject& object : data.objects) {
        std::uint64_t validCount = 0;
        for (const Option& option : object.options) validCount += option.valid;
        if (validCount == 0) return {};
        if (combinations > exactLimit / validCount) {
            combinations = exactLimit + 1;
            continue;
        }
        combinations *= validCount;
    }

    if (combinations == 1) {
        std::vector<int> choice(data.objects.size(), -1);
        for (std::size_t i = 0; i < data.objects.size(); ++i)
            for (std::size_t j = 0; j < data.objects[i].options.size(); ++j)
                if (data.objects[i].options[j].valid) { choice[i] = static_cast<int>(j); break; }
        Solution result;
        evaluateChoice(data, choice, result);
        return result;
    }
    if (combinations <= exactLimit && data.objects.size() <= 24) {
        Solution best;
        std::vector<int> choice(data.objects.size(), -1);
        exactDfs(data, 0, 0, 0, 0, 0, choice, best);
        return best;
    }
    return lagrangianHeuristic(data);
}

bool readCase(CaseData& data) {
    int objectCount = 0;
    if (!input.read(objectCount, data.capacityBytes,
          data.compressBudgetNs, data.wr, data.wc, data.ws)) {
        return false;
    }
    if (objectCount < 0 || data.capacityBytes < 0 ||
        data.compressBudgetNs < 0 || data.wr < 0 || data.wc < 0 || data.ws < 0) {
        return false;
    }
    data.objects.resize(static_cast<std::size_t>(objectCount));

    for (DataObject& object : data.objects) {
        int optionCount = 0;
        if (!input.read(object.objectId, object.dataType,
              object.originalBytes, object.accessFrequency,
              object.priority, object.maxRestoreLatencyNs, optionCount)) {
            return false;
        }
        if (optionCount < 0 || object.originalBytes < 0 || object.accessFrequency < 0 ||
            object.priority < 0 || object.maxRestoreLatencyNs < 0) return false;
        object.options.resize(static_cast<std::size_t>(optionCount));

        for (Option& option : object.options) {
            int stageCount = 0;
            if (!input.read(option.optionId, stageCount) ||
                stageCount < 0 || stageCount > 2) {
                return false;
            }
            option.stages.resize(static_cast<std::size_t>(stageCount));
            for (Stage& stage : option.stages) {
                if (!input.read(stage.algorithmId, stage.inputBytes,
                      stage.outputBytes, stage.compressTimeNs,
                      stage.decompressTimeNs)) {
                    return false;
                }
                if (stage.algorithmId < 1 || stage.algorithmId > 3 ||
                    stage.inputBytes < 0 || stage.outputBytes < 0 ||
                    stage.compressTimeNs < 0 || stage.decompressTimeNs < 0) {
                    option.valid = false;
                }
            }

            if (stageCount == 0) {
                option.finalBytes = object.originalBytes;
            } else {
                if (stageCount == 2 && option.stages[0].algorithmId == option.stages[1].algorithmId)
                    option.valid = false;
                option.valid = option.valid &&
                    option.stages.front().inputBytes == object.originalBytes;
                for (int s = 1; s < stageCount; ++s) {
                    option.valid = option.valid &&
                        option.stages[s - 1].outputBytes ==
                            option.stages[s].inputBytes;
                }
                option.finalBytes = option.stages.back().outputBytes;
                for (const Stage& stage : option.stages) {
                    option.compressTimeNs += stage.compressTimeNs;
                    option.decompressTimeNs += stage.decompressTimeNs;
                }
            }
            option.valid = option.valid &&
                option.decompressTimeNs <= object.maxRestoreLatencyNs;
            option.cost =
                static_cast<i128>(data.wr) * object.priority *
                    object.accessFrequency * option.decompressTimeNs +
                static_cast<i128>(data.wc) * option.compressTimeNs +
                static_cast<i128>(data.ws) * option.finalBytes;
        }
    }
    return true;
}

}  // namespace

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    int caseCount = 0;
    if (!input.read(caseCount) || caseCount < 0) {
        std::cerr << "Invalid test case count.\n";
        return 1;
    }

    for (int caseIndex = 1; caseIndex <= caseCount; ++caseIndex) {
        CaseData data;
        if (!readCase(data)) {
            std::cerr << "Invalid input in case " << caseIndex << ".\n";
            return 1;
        }

        const Solution answer = solve(data);
        if (!answer.feasible) {
            std::cerr << "No feasible solution found for case " << caseIndex
                      << ". The large-case demo heuristic may need improvement.\n";
            return 2;
        }

        std::cout << "Case #" << caseIndex << ":\n";
        for (std::size_t i = 0; i < data.objects.size(); ++i) {
            const DataObject& object = data.objects[i];
            const Option& option = object.options[answer.optionIndexes[i]];
            std::cout << object.objectId << ' ' << option.optionId << '\n';
        }
        std::cerr << "Case #" << caseIndex
                  << " cost=" << toString(answer.totalCost)
                  << " bytes=" << toString(answer.totalBytes)
                  << " compress_ns=" << toString(answer.totalCompressTimeNs) << '\n';
    }
    return 0;
}
