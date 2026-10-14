#include <algorithm>
#include <cstdint>
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
    i64 compressTimeNs = 0;
    i64 decompressTimeNs = 0;
    i128 cost = 0;
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
    i64 totalBytes = 0;
    i64 totalCompressTimeNs = 0;
    i64 totalDecompressTimeNs = 0;
    i128 totalCost = 0;
};

namespace {

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
void exactDfs(const CaseData& data, std::size_t index, i64 usedBytes,
              i64 usedTime, i64 usedRestore, i128 cost,
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

// For large inputs, independently minimize a Lagrangian-penalized cost.
// This is a demonstration heuristic, not a proof of optimality.
Solution lagrangianHeuristic(const CaseData& data) {
    Solution best;
    const long double byteScale =
        std::max<i64>(1, data.capacityBytes) / 1000.0L;
    const long double timeScale =
        std::max<i64>(1, data.compressBudgetNs) / 1000.0L;

    for (int byteStep = 0; byteStep <= 80; ++byteStep) {
        for (int timeStep = 0; timeStep <= 80; ++timeStep) {
            const long double lambdaBytes = byteStep / byteScale;
            const long double lambdaTime = timeStep / timeScale;
            std::vector<int> choice(data.objects.size(), -1);

            for (std::size_t i = 0; i < data.objects.size(); ++i) {
                long double bestPenalized =
                    std::numeric_limits<long double>::infinity();
                for (std::size_t j = 0; j < data.objects[i].options.size(); ++j) {
                    const Option& option = data.objects[i].options[j];
                    if (!option.valid) continue;
                    const long double penalized =
                        static_cast<long double>(option.cost) +
                        lambdaBytes * option.finalBytes +
                        lambdaTime * option.compressTimeNs;
                    if (penalized < bestPenalized) {
                        bestPenalized = penalized;
                        choice[i] = static_cast<int>(j);
                    }
                }
            }

            Solution candidate;
            evaluateChoice(data, choice, candidate);
            if (better(candidate, best, data.objects)) best = std::move(candidate);
        }
    }
    return best;
}

Solution solve(const CaseData& data) {
    std::uint64_t combinations = 1;
    constexpr std::uint64_t exactLimit = 5'000'000;
    for (const DataObject& object : data.objects) {
        std::uint64_t validCount = 0;
        for (const Option& option : object.options) validCount += option.valid;
        if (validCount == 0) return {};
        if (combinations > exactLimit / validCount) {
            combinations = exactLimit + 1;
            break;
        }
        combinations *= validCount;
    }

    if (combinations <= exactLimit) {
        Solution best;
        std::vector<int> choice(data.objects.size(), -1);
        exactDfs(data, 0, 0, 0, 0, 0, choice, best);
        return best;
    }
    return lagrangianHeuristic(data);
}

bool readCase(CaseData& data) {
    int objectCount = 0;
    if (!(std::cin >> objectCount >> data.capacityBytes >>
          data.compressBudgetNs >> data.wr >> data.wc >> data.ws)) {
        return false;
    }
    if (objectCount < 0 || data.capacityBytes < 0 ||
        data.compressBudgetNs < 0) {
        return false;
    }
    data.objects.resize(static_cast<std::size_t>(objectCount));

    for (DataObject& object : data.objects) {
        int optionCount = 0;
        if (!(std::cin >> object.objectId >> object.dataType >>
              object.originalBytes >> object.accessFrequency >>
              object.priority >> object.maxRestoreLatencyNs >> optionCount)) {
            return false;
        }
        if (optionCount < 0 || object.originalBytes < 0) return false;
        object.options.resize(static_cast<std::size_t>(optionCount));

        for (Option& option : object.options) {
            int stageCount = 0;
            if (!(std::cin >> option.optionId >> stageCount) ||
                stageCount < 0 || stageCount > 2) {
                return false;
            }
            option.stages.resize(static_cast<std::size_t>(stageCount));
            for (Stage& stage : option.stages) {
                if (!(std::cin >> stage.algorithmId >> stage.inputBytes >>
                      stage.outputBytes >> stage.compressTimeNs >>
                      stage.decompressTimeNs)) {
                    return false;
                }
                if (stage.inputBytes < 0 || stage.outputBytes < 0 ||
                    stage.compressTimeNs < 0 || stage.decompressTimeNs < 0) {
                    option.valid = false;
                }
            }

            if (stageCount == 0) {
                option.finalBytes = object.originalBytes;
            } else {
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
    if (!(std::cin >> caseCount) || caseCount < 0) {
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
                  << " bytes=" << answer.totalBytes
                  << " compress_ns=" << answer.totalCompressTimeNs << '\n';
    }
    return 0;
}
