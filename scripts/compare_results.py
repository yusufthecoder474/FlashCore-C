#!/usr/bin/env python3
"""Compare normalized summaries; never assume unprovided C++ CSV semantics."""
import argparse
import csv
import sys
from pathlib import Path

KEY = ('workload', 'seed')
META = ('blocks', 'pages_per_block', 'lbas', 'workload_sha256', 'metric_version')
METRICS = ('host_writes', 'physical_programs', 'gc_migrations', 'erases', 'write_amplification')


def load(path):
    with path.open(newline='') as stream:
        reader = csv.DictReader(stream)
        missing = set(KEY + META + METRICS) - set(reader.fieldnames or [])
        if missing:
            raise ValueError(f'{path}: missing columns {sorted(missing)}')
        result = {}
        for row in reader:
            key = tuple(row[k] for k in KEY)
            if key in result:
                raise ValueError(f'duplicate workload/seed: {key}')
            result[key] = row
        if not result:
            raise ValueError('empty summary')
        return result


def compare(left, right):
    messages = []
    for key in sorted(left.keys() | right.keys()):
        if key not in left or key not in right:
            messages.append(f'{key}: missing workload/seed')
            continue
        a, b = left[key], right[key]
        for column in META + METRICS:
            if column == 'write_amplification':
                import math
                av, bv = float(a[column]), float(b[column])
                same = math.isfinite(av) and math.isfinite(bv) and abs(av - bv) <= .000001
            else:
                same = a[column] == b[column]
            if not same:
                messages.append(f'{key} {column}: {a[column]} != {b[column]}')
    return messages


def main():
    p = argparse.ArgumentParser()
    p.add_argument('left', type=Path)
    p.add_argument('right', type=Path)
    p.add_argument('--report', type=Path, default=Path('comparison.md'))
    a = p.parse_args()
    try:
        differences = compare(load(a.left), load(a.right))
    except (OSError, ValueError, KeyError) as error:
        differences = [f'Invalid comparison input: {error}']
    a.report.write_text('# CSV comparison\n\n' + ('\n'.join('- ' + d for d in differences) if differences else 'MATCH: all rows and selected fields agree.') + '\n')
    print(f'{len(differences)} mismatches; report: {a.report}')
    return bool(differences)


if __name__ == '__main__':
    sys.exit(main())
