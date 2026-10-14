"""Reproducible synthetic evaluation; Python 3 + scipy (medium exact oracle).
Run: python evaluate.py   (build C++ executables first; see README_eval.md)
All feasibility and objective checks recompute from input stages, not solver logs.
"""
import ctypes
import itertools
import json
import math
from pathlib import Path
import random
import subprocess
import time

ROOT = Path(__file__).resolve().parent
OUT = ROOT / 'evaluation'
OUT.mkdir(exist_ok=True)

def metrics(obj, opt, weights):
    oid, typ, size, freq, priority, limit, options = obj
    option_id, stages = opt
    valid = len(stages) <= 2
    if stages:
        valid &= stages[0][1] == size
        valid &= all(a[2] == b[1] for a, b in zip(stages, stages[1:]))
        valid &= all(1 <= s[0] <= 3 and all(v >= 0 for v in s[1:]) for s in stages)
        valid &= len(stages) != 2 or stages[0][0] != stages[1][0]
    b = stages[-1][2] if stages else size
    t = sum(s[3] for s in stages)
    d = sum(s[4] for s in stages)
    valid &= d <= limit
    wr, wc, ws = weights
    return valid, b, t, d, wr * freq * priority * d + wc * t + ws * b

def generate(n, seed, group, count=10):
    rng = random.Random(seed)
    weights = {'A': (1, 1, 100), 'B': (0, 1, 300), 'C': (10, 50, 200),
               'D': (1, 1, 1000), 'E': (1, 10, 300), 'F': (0, 0, 0)}[group]
    objects, wb, wt = [], 0, 0
    for i in range(n):
        typ = rng.randrange(6)
        size = rng.randint(1000, 100000)
        freq = rng.choice([0, 1, 5, 100, 10000])
        priority = rng.randint(1, 10)
        limit = rng.choice([50, 200, 1000, 100000]) if group in 'CF' else 100000
        options = [(0, [])]
        # Type-dependent measured outcomes with noise; already-compressed images
        # have less benefit. Ordered two-stage outcomes are generated separately.
        ratios = [0.80, 0.57, 0.36] if typ != 2 else [0.98, 0.94, 0.90]
        for alg in range(1, 4):
            b = max(1, int(size * min(1.05, ratios[alg-1] * rng.uniform(.85, 1.15))))
            t = int(size * [0.08, .22, .65][alg-1] * rng.uniform(.8, 1.2))
            d = int(size * [.001, .004, .013][alg-1] * rng.uniform(.8, 1.2))
            options.append((alg, [(alg, size, b, t, d)]))
        for a, b in itertools.permutations(range(1, 4), 2):
            first = options[a][1][0]
            middle = first[2]
            final = max(1, int(middle * rng.uniform(.60, .98)))
            second = (b, middle, final, int(middle * [.08, .22, .65][b-1]),
                      int(middle * [.001, .004, .013][b-1]))
            options.append((len(options), [first, second]))
        options = options[:count]
        obj = (i + 1, typ, size, freq, priority, limit, options)
        valid = [m for o in options if (m := metrics(obj, o, weights))[0]]
        witness = rng.choice(valid)
        wb += witness[1]
        wt += witness[2]
        objects.append(obj)
    # Every generated instance has a known feasible witness, discarded by solver.
    slack = 0 if group == 'F' else .03
    return (int(wb * (1 + slack)), int(wt * (1 + slack)), weights, objects)

def write_case(case, path):
    cap, budget, weights, objects = case
    with path.open('w', encoding='utf-8', newline='\n') as f:
        f.write('1\n' + ' '.join(map(str, (len(objects), cap, budget, *weights))) + '\n')
        for obj in objects:
            f.write(' '.join(map(str, (*obj[:6], len(obj[6])))) + '\n')
            for oid, stages in obj[6]:
                f.write(f'{oid} {len(stages)}\n')
                for stage in stages:
                    f.write(' '.join(map(str, stage)) + '\n')

class MemoryCounters(ctypes.Structure):
    _fields_ = [('cb', ctypes.c_ulong), ('PageFaultCount', ctypes.c_ulong),
               ('PeakWorkingSetSize', ctypes.c_size_t), ('WorkingSetSize', ctypes.c_size_t),
               ('QuotaPeakPagedPoolUsage', ctypes.c_size_t), ('QuotaPagedPoolUsage', ctypes.c_size_t),
               ('QuotaPeakNonPagedPoolUsage', ctypes.c_size_t), ('QuotaNonPagedPoolUsage', ctypes.c_size_t),
               ('PagefileUsage', ctypes.c_size_t), ('PeakPagefileUsage', ctypes.c_size_t)]

def run(exe, path, timeout=15):
    stdout, stderr = path.with_suffix('.' + exe + '.out'), path.with_suffix('.' + exe + '.err')
    peak = 0
    start = time.perf_counter()
    with path.open('rb') as fi, stdout.open('wb') as fo, stderr.open('wb') as fe:
        p = subprocess.Popen([str(ROOT / (exe + '.exe'))], stdin=fi, stdout=fo, stderr=fe)
        while p.poll() is None:
            if time.perf_counter() - start > timeout:
                p.kill(); p.wait()
                return {'status': 'timeout', 'seconds': timeout, 'peak_mb': peak}
            if hasattr(ctypes, 'windll'):
                counters = MemoryCounters(); counters.cb = ctypes.sizeof(counters)
                if ctypes.windll.psapi.GetProcessMemoryInfo(ctypes.c_void_p(int(p._handle)),
                        ctypes.byref(counters), counters.cb):
                    peak = max(peak, counters.PeakWorkingSetSize / 2**20)
            time.sleep(.01)
    return {'status': 'ok' if p.returncode == 0 else f'exit_{p.returncode}',
            'seconds': time.perf_counter() - start, 'peak_mb': round(peak, 2),
            'output': str(stdout.name)}

def check(case, result):
    if result['status'] != 'ok': return result
    cap, budget, weights, objects = case
    lines = (OUT / result['output']).read_text().splitlines()
    try:
        assert lines[0] == 'Case #1:' and len(lines) == len(objects) + 1
        totals = [0, 0, 0, 0]
        for line, obj in zip(lines[1:], objects):
            object_id, option_id = map(int, line.split())
            assert object_id == obj[0]
            selected = [o for o in obj[6] if o[0] == option_id]
            assert len(selected) == 1
            valid, *values = metrics(obj, selected[0], weights)
            assert valid
            totals = [a + b for a, b in zip(totals, values)]
        assert totals[0] <= cap and totals[1] <= budget
        result.update(bytes=totals[0], compress_ns=totals[1], restore_ns=totals[2], cost=totals[3])
    except (AssertionError, ValueError, IndexError):
        result['status'] = 'invalid'
    return result

def brute(case):
    cap, budget, w, objects = case
    groups = [[(m[4], m[1], m[2], m[3], o[0]) for o in obj[6]
               if (m := metrics(obj, o, w))[0]] for obj in objects]
    best = None
    for combo in itertools.product(*groups):
        c, b, t, d = (sum(x[j] for x in combo) for j in range(4))
        key = (c, b, t, d, tuple(x[4] for x in combo))
        if b <= cap and t <= budget and (best is None or key < best): best = key
    return best

def oracle(case):
    import numpy as np
    from scipy.optimize import milp, Bounds, LinearConstraint
    from scipy.sparse import coo_matrix
    cap, budget, w, objects = case
    costs, rows, cols, vals, metadata = [], [], [], [], []
    for i, obj in enumerate(objects):
        for opt in obj[6]:
            m = metrics(obj, opt, w)
            if not m[0]: continue
            j = len(costs); costs.append(m[4]); metadata.append(m)
            rows.extend([i, len(objects), len(objects)+1]); cols.extend([j]*3)
            vals.extend([1, m[1] / max(1, cap), m[2] / max(1, budget)])
    matrix = coo_matrix((vals, (rows, cols)), shape=(len(objects)+2, len(costs))).tocsc()
    result = milp(np.array(costs, dtype=float) / max(1, max(costs)), integrality=np.ones(len(costs)),
                  bounds=Bounds(0, 1), constraints=LinearConstraint(matrix,
                  [1]*len(objects)+[-np.inf]*2, [1]*len(objects)+[1,1]),
                  options={'time_limit': 20, 'mip_rel_gap': 0})
    if result.x is None: return {'status': int(result.status)}
    chosen = [m for m, x in zip(metadata, result.x) if x > .5]
    assert len(chosen) == len(objects)
    assert sum(m[1] for m in chosen) <= cap and sum(m[2] for m in chosen) <= budget
    return {'status': int(result.status), 'cost': sum(m[4] for m in chosen),
            'mip_gap': float(result.mip_gap)}

def greedy(case):
    """Capacity savings/compression time baseline, additionally respecting time."""
    cap, budget, w, objects = case
    choices = [metrics(obj, obj[6][0], w) for obj in objects]
    b = sum(m[1] for m in choices); t = sum(m[2] for m in choices)
    edges = []
    for i, obj in enumerate(objects):
        for opt in obj[6][1:]:
            m = metrics(obj, opt, w)
            if m[0] and m[1] < obj[2]:
                edges.append(((obj[2] - m[1]) / max(1, m[2]), i, m))
    edges.sort(key=lambda e: e[0], reverse=True)
    for ratio, i, m in edges:
        if b <= cap: break
        old = choices[i]
        if m[1] >= old[1] or t - old[2] + m[2] > budget: continue
        b += m[1] - old[1]; t += m[2] - old[2]; choices[i] = m
    return {'status': 'ok' if b <= cap and t <= budget else 'infeasible',
            'cost': sum(m[4] for m in choices), 'bytes': b, 'compress_ns': t}

def main():
    records = []
    def evaluate(name, group, case, exact=None, baseline=True, timeout=15):
        path = OUT / (name + '.txt'); write_case(case, path)
        row = {'name': name, 'group': group, 'n': len(case[3]), 'capacity': case[0],
               'budget': case[1], 'solver': check(case, run('15_solver', path, timeout))}
        row['greedy'] = greedy(case)
        if row['greedy']['status'] == 'ok' and row['solver']['status'] == 'ok':
            row['greedy_improvement_pct'] = 100 * (row['greedy']['cost'] - row['solver']['cost']) / max(1, row['greedy']['cost'])
        if baseline: row['baseline'] = check(case, run('15_baseline', path, min(timeout, 3)))
        if exact == 'brute':
            target = brute(case)
            row['reference'] = {'status': 0, 'cost': target[0], 'tie_key': target}
            r = row['solver']
            if r['status'] == 'ok':
                ids = tuple(int(s.split()[1]) for s in (OUT/r['output']).read_text().splitlines()[1:])
                row['tie_match'] = (r['cost'],r['bytes'],r['compress_ns'],r['restore_ns'],ids) == target
        elif exact == 'milp': row['reference'] = oracle(case)
        if row.get('reference', {}).get('status') == 0 and row['solver']['status'] == 'ok':
            target = row['reference']['cost']
            row['gap_pct'] = 100 * (row['solver']['cost'] - target) / max(1, target)
        records.append(row)
        (OUT / 'results.json').write_text(json.dumps(records, ensure_ascii=False, indent=2), encoding='utf-8')
        print(name, row['solver']['status'], round(row['solver']['seconds'],3),
              'baseline:',row.get('baseline',{}).get('status'), 'gap:',row.get('gap_pct'), flush=True)
    for group in 'ABCDEF':
        for seed in range(6):
            evaluate(f'small_{group}_{seed}', group, generate(6, 1500+seed, group, 4), 'brute')
    for group in 'BCDE':
        for seed in range(3):
            evaluate(f'medium_{group}_{seed}', group, generate(80, 2500+seed, group), 'milp')
    # Invalid stage chains, latency violations, repeated algorithms, tie IDs,
    # 64-bit aggregate overflow, and deeply fixed choices.
    obj = (1,0,100,1,1,10, [(9,[]),(2,[]),(3,[(1,101,1,0,0)]),
             (4,[(1,100,1,1,11)]),(5,[(1,100,50,1,1),(1,50,1,1,1)])])
    evaluate('edge_invalid_ties','F',(100,0,(0,0,0),[obj]),'brute')
    obj2=(1,0,100,1,1,10,[(0,[]),(1,[(1,100,50,1,1),(2,51,1,1,1)])])
    evaluate('edge_broken_chain','F',(100,0,(1,1,1),[obj2]),'brute')
    huge=5_000_000_000_000_000_000
    objects=[(i,0,huge,1,1,100,[(0,[]),(1,[(1,huge,1,1,1)])]) for i in range(1,4)]
    evaluate('edge_int64_totals','F',(huge,100,(1,1,1),objects),'brute')
    fixed=[(i,0,1,1,1,0,[(0,[])]) for i in range(100000)]
    evaluate('edge_fixed_100k','F',(100000,0,(1,1,1),fixed),baseline=False)
    for group, n in [('B',200),('C',1000),('D',10000),('E',100000),('F',100000)]:
        evaluate(f'scale_{group}_{n}',group,generate(n,3515,group),timeout=20)
    # Three identical runs to expose timing variability.
    for repeat in range(2):
        evaluate(f'repeat_E_{repeat}','E',generate(100000,3515,'E'),baseline=False,timeout=20)
    print('Saved evaluation/results.json', flush=True)

if __name__ == '__main__': main()
