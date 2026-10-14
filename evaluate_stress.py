"""Independent tightly constrained, unstructured candidates (distribution shift)."""
import json
import random
import evaluate as e

rows = []
for seed in range(20):
    rng = random.Random(4500 + seed)
    objects, cap, budget = [], 0, 0
    weights = (1, 1, rng.choice([1, 10, 100]))
    for i in range(30):
        opts = [(0, [])]
        for j in range(1, 4):
            opts.append((j, [(j, 100, rng.randint(1, 99), rng.randint(0, 100), rng.randint(0, 100))]))
        obj = (i+1, rng.randrange(6), 100, rng.randint(1, 10), rng.randint(1, 10), 100, opts)
        m = e.metrics(obj, rng.choice(opts), weights)
        cap += m[1]; budget += m[2]; objects.append(obj)
    case = (cap, budget, weights, objects)
    path = e.OUT / f'stress_{seed}.txt'; e.write_case(case, path)
    result = e.check(case, e.run('15_solver', path))
    reference = e.oracle(case)
    row = {'name': f'stress_{seed}', 'solver': result, 'reference': reference}
    if result['status'] == 'ok' and reference['status'] == 0:
        row['gap_pct'] = 100 * (result['cost'] - reference['cost']) / max(1, reference['cost'])
    rows.append(row)
    (e.OUT/'stress_results.json').write_text(json.dumps(rows, indent=2), encoding='utf-8')
    print(seed, result['status'], row.get('gap_pct'), flush=True)
