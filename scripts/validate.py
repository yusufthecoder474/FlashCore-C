#!/usr/bin/env python3
"""Standard-library workload generator, independent oracle and evidence writer."""
import argparse
import csv
import hashlib
import io
import json
import platform
import random
import subprocess
import sys
import time
from pathlib import Path


def workloads():
    cases = []
    for pattern in ('uniform', 'hotcold', 'trim_mix'):
        for seed in (7, 19, 41):
            rng = random.Random(seed)
            ops = [('WRITE', i, i) for i in range(32)]
            for _ in range(2000):
                lba = rng.randrange(8 if pattern == 'hotcold' and rng.random() < .8 else 32)
                op = rng.choices(['WRITE', 'READ', 'TRIM'], [7, 2, 2 if pattern == 'trim_mix' else 0])[0]
                ops.append((op, lba, rng.getrandbits(32) if op == 'WRITE' else 0))
            cases.append((pattern, seed, ops, {}))
    fixture = [('WRITE', i, i) for i in range(32)] + [('WRITE', i, i + 100) for i in range(0, 32, 4)]
    cases += [
        ('sequential', 0, [('WRITE', i % 32, i) for i in range(256)], {}),
        ('program_fault', 0, [('WRITE', 0, 9), ('FAULT_PROGRAM', 0, 1), ('WRITE', 0, 10),
                              ('READ', 0, 0), ('WRITE', 0, 11)], {2: 'PROGRAM'}),
        ('gc_rollback', 0, fixture + [('FAULT_PROGRAM', 0, 2), ('GC', 0, 0), ('GC', 0, 0)], {41: 'PROGRAM'}),
        ('erase_fault', 0, fixture + [('FAULT_ERASE', 0, 1), ('GC', 0, 0)], {41: 'ERASE'}),
        ('retirement', 0, [('RETIRE', 1, 0), ('RETIRE', 0, 0), ('RETIRE', 7, 0)] +
         [('WRITE', i, i) for i in range(16)], {1: 'BUSY', 2: 'BUSY'}),
        ('bounds', 0, [('WRITE', 32, 1), ('READ', 65535, 0), ('TRIM', 32, 0), ('RETIRE', 8, 0)],
         {0: 'RANGE', 1: 'RANGE', 2: 'RANGE', 3: 'RANGE'}),
    ]
    return cases


def check(ops, expected_errors, output):
    rows = list(csv.DictReader(io.StringIO(output)))
    assert len(rows) == len(ops), 'row count'
    model = {}
    host = 0
    previous = dict.fromkeys(('physical_programs', 'gc_migrations', 'erases'), 0)
    for i, ((op, lba, value), row) in enumerate(zip(ops, rows)):
        expected = expected_errors.get(i, 'UNMAPPED' if op == 'READ' and lba not in model else 'OK')
        assert row['status'] == expected, (i, op, row['status'], expected)
        assert int(row['op']) == i + 1 and row['command'] == op
        if op == 'WRITE' and expected == 'OK':
            model[lba] = value
            host += 1
        if op == 'TRIM' and expected == 'OK':
            model.pop(lba, None)
        if op == 'READ' and expected == 'OK':
            assert int(row['value']) == model[lba], ('read mismatch', i)
        assert int(row['host_writes']) == host
        for key in previous:
            current = int(row[key])
            assert current >= previous[key], (key, i)
            previous[key] = current
        physical = int(row['physical_programs'])
        assert physical >= host + int(row['gc_migrations'])
        assert abs(float(row['write_amplification']) - (physical / host if host else 0)) < .000001
    return rows[-1], host


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', required=True, type=Path)
    parser.add_argument('--out', default=Path('validation'), type=Path)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    exe = str(args.exe.resolve())
    summary, failures = [], []
    commands, checks = [], 0
    for name, seed, original, errors in workloads():
        # Read all LBAs after EVERY operation: a separate Python array/dict oracle
        # observes the CLI's actual reads, independently of its internal C oracle.
        ops, expanded_errors = [], {}
        for i, cmd in enumerate(original):
            if i in errors:
                expanded_errors[len(ops)] = errors[i]
            ops.append(cmd)
            ops.extend(('READ', lba, 0) for lba in range(32))
        payload = ''.join(f'{op},{lba},{value}\n' for op, lba, value in ops)
        stem = f'{name}-{seed}'
        (args.out / f'{stem}.input.csv').write_text(payload)
        durations, outputs, codes = [], [], []
        try:
            for _ in range(2):
                start = time.perf_counter()
                result = subprocess.run([exe], input=payload, text=True, capture_output=True, timeout=60)
                durations.append(time.perf_counter() - start)
                codes.append(result.returncode)
                outputs.append(result.stdout)
                assert result.returncode == 0, result.stderr
                assert not result.stderr, result.stderr
            assert outputs[0] == outputs[1], 'nondeterministic CSV'
            (args.out / f'{stem}.result.csv').write_text(outputs[0])
            last, host = check(ops, expanded_errors, outputs[0])
            checks += len(ops)
            summary.append(dict(workload=name, seed=seed, blocks=8, pages_per_block=8, lbas=32,
                workload_sha256=hashlib.sha256(payload.encode()).hexdigest(),
                metric_version='successful-programs-v1', operations=len(original),
                checked_rows=len(ops), host_writes=host, physical_programs=last['physical_programs'],
                gc_migrations=last['gc_migrations'], erases=last['erases'],
                write_amplification=last['write_amplification']))
            commands.append(dict(workload=stem, argv=[exe], exit_codes=codes, seconds=durations))
        except (AssertionError, subprocess.TimeoutExpired) as error:
            failures.append(f'{stem}: {error}')
    invalid = ['BOGUS,0,0\n', 'WRITE,-1,2\n', 'WRITE,0,4294967296\n',
               'WRITE,0,1,2\n', 'WRITE,0\n', 'X' * 200 + '\n', 'READ,65536,0\n']
    for payload in invalid:
        r = subprocess.run([exe], input=payload, text=True, capture_output=True, timeout=10)
        if r.returncode != 2 or 'malformed' not in r.stderr:
            failures.append(f'malformed-input rejection: {payload[:30]!r}, exit {r.returncode}')
    ram = subprocess.run([exe, '--ram'], text=True, capture_output=True, timeout=10)
    if ram.returncode != 0:
        failures.append(f'RAM query exit {ram.returncode}: {ram.stderr}')
    else:
        (args.out / 'ram.csv').write_text(ram.stdout)
        data = list(csv.DictReader(io.StringIO(ram.stdout)))
        if len(data) != 1 or int(data[0]['map_bytes']) != 64 or int(data[0]['state_bytes']) != 64:
            failures.append('RAM dimensions mismatch')
    if summary:
        with (args.out / 'summary.csv').open('w', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=summary[0].keys())
            writer.writeheader()
            writer.writerows(summary)
    record = dict(platform=platform.platform(), python=sys.version, runs=commands,
                  scenarios=len(summary), checked_rows=checks, malformed_cases=len(invalid), failures=failures)
    (args.out / 'runs.json').write_text(json.dumps(record, indent=2))
    report = ['# FlashCore-C validation', '', f'Executable: `{exe}`',
              f'Workloads passed: {len(summary)}/{len(workloads())}; two identical runs each.',
              f'Checked CSV rows per pass: {checks}. Malformed-input cases: {len(invalid)}.',
              'Each original command is followed by reads of all 32 LBAs against a Python model.',
              'CLI also compares its C reference array and structural invariants after every command.',
              'Timings in runs.json include process startup, CSV I/O and oracle work in C; not SSD performance.',
              '', 'Failures: ' + ('; '.join(failures) if failures else 'none'), '',
              '| Workload | Seed | Host writes | Programs | GC copies | Erases | WA |',
              '|---|---:|---:|---:|---:|---:|---:|']
    report += [f"| {r['workload']} | {r['seed']} | {r['host_writes']} | {r['physical_programs']} | {r['gc_migrations']} | {r['erases']} | {r['write_amplification']} |" for r in summary]
    (args.out / 'REPORT.md').write_text('\n'.join(report) + '\n')
    print(f'FlashCore: {len(summary)}/{len(workloads())} workloads, {checks} checked rows; failures={len(failures)}')
    return bool(failures)


if __name__ == '__main__':
    sys.exit(main())
