import json, math, re, subprocess, sys, time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(r'H:\rubik-solver')
EXE = ROOT / 'build-final2' / 'rubik-solver.exe'
OUT = ROOT / '.scratch' / 'final2'
OUT.mkdir(parents=True, exist_ok=True)

scrambles = [l.strip() for l in (ROOT / 'benchmarks' / 'baseline' / 'scrambles.txt').read_text().splitlines() if l.strip()]
assert len(scrambles) == 50, len(scrambles)

commit = subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=ROOT, capture_output=True, text=True).stdout.strip()
branch = subprocess.run(['git', 'branch', '--show-current'], cwd=ROOT, capture_output=True, text=True).stdout.strip()

METHODS = ['cfop', 'roux', 'zz', 'heise', 'twophase']
rows = []

def median(vals):
    if not vals:
        return None
    s = sorted(vals)
    n = len(s)
    return s[n // 2] if n % 2 == 1 else (s[n // 2 - 1] + s[n // 2]) / 2

def p95(vals):
    if not vals:
        return None
    s = sorted(vals)
    return s[math.ceil(0.95 * len(s)) - 1]

def fmt(x):
    return int(x) if x is not None and float(x) == int(x) else x

t_all = time.time()
for m in METHODS:
    for i, scr in enumerate(scrambles, 1):
        cmd = [str(EXE), '--method', m, '--scramble', scr, '--timeout-ms', '10000']
        if m == 'twophase':
            cmd += ['--tables-dir', str(ROOT / 'build' / 'tables')]
        t0 = time.time()
        try:
            p = subprocess.run(cmd, capture_output=True, text=True, timeout=20)
            wall = round((time.time() - t0) * 1000)
            out = p.stdout + p.stderr
            el = re.search(r'elapsed_ms:\s*(\d+)', out)
            tm = re.search(r'total_moves:\s*(\d+)', out)
            err = re.search(r'error:\s*(.+)', out)
            if tm and not err:
                row = {'method': m, 'scramble_index': i,
                       'time_ms': int(el.group(1)) if el else wall,
                       'moves': int(tm.group(1)), 'status': 'ok'}
            else:
                status = 'fail:' + (err.group(1).strip() if err else 'Exit' + str(p.returncode))
                row = {'method': m, 'scramble_index': i,
                       'time_ms': int(el.group(1)) if el else wall,
                       'moves': None, 'status': status}
        except subprocess.TimeoutExpired:
            row = {'method': m, 'scramble_index': i, 'time_ms': 20000,
                   'moves': None, 'status': 'fail:ShellTimeout'}
        rows.append(row)
        (OUT / 'rows.json').write_text(json.dumps(rows, indent=1))
        print(f'{m} #{i} {row["status"]} {row["moves"]} {row["time_ms"]}ms elapsed={time.time()-t_all:.0f}s', flush=True)

per_method = {}
for m in METHODS:
    rr = [r for r in rows if r['method'] == m]
    succ = [r for r in rr if r['status'] == 'ok']
    times = [r['time_ms'] for r in succ]
    moves = [r['moves'] for r in succ]
    per_method[m] = {
        'runs': len(rr),
        'successes': len(succ),
        'failures': len(rr) - len(succ),
        'median_time_ms': fmt(median(times)),
        'p95_time_ms': fmt(p95(times)),
        'mean_moves': round(sum(moves) / len(moves), 2) if moves else None,
        'failure_statuses': [r['status'] for r in rr if r['status'] != 'ok'],
    }

result = {
    'seed': 42,
    'generator': 'Node mulberry32(seed=42); 50 scrambles x 25 moves drawn uniformly from 18 face moves (U,U\',U2,R,R\',R2,F,F\',F2,D,D\',D2,L,L\',L2,B,B\',B2), no same face twice in a row',
    'scrambles_path': 'benchmarks/baseline/scrambles.txt',
    'depth': 25,
    'executable': str(EXE),
    'git_commit': commit,
    'git_branch': branch,
    'date': datetime.now(timezone.utc).isoformat().replace('+00:00', 'Z'),
    'per_run_timeout_ms': 10000,
    'shell_timeout_ms': 20000,
    'per_method': per_method,
    'rows': rows,
}
(OUT / 'results.json').write_text(json.dumps(result, indent=1))
print('DONE')
print(json.dumps(per_method, indent=1))
