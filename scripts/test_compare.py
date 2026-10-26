#!/usr/bin/env python3
import csv
import tempfile
import unittest
from pathlib import Path
from compare_results import KEY, META, METRICS, compare, load


class ComparisonTests(unittest.TestCase):
    def setUp(self):
        self.row = dict(zip(KEY + META + METRICS,
            ('uniform', '7', '8', '8', '32', 'abc', 'successful-programs-v1', '10', '12', '2', '1', '1.2')))
        self.key = ('uniform', '7')

    def test_identical(self):
        self.assertEqual(compare({self.key: self.row}, {self.key: dict(self.row)}), [])

    def test_metric(self):
        changed = dict(self.row, physical_programs='13')
        self.assertEqual(len(compare({self.key: self.row}, {self.key: changed})), 1)

    def test_metadata(self):
        changed = dict(self.row, blocks='9', workload_sha256='different')
        self.assertEqual(len(compare({self.key: self.row}, {self.key: changed})), 2)

    def test_missing(self):
        self.assertEqual(len(compare({self.key: self.row}, {})), 1)

    def test_nonfinite(self):
        changed = dict(self.row, write_amplification='nan')
        self.assertEqual(len(compare({self.key: self.row}, {self.key: changed})), 1)

    def test_invalid_files(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'bad.csv'
            path.write_text('unexpected\n1\n')
            with self.assertRaises(ValueError):
                load(path)
            with path.open('w', newline='') as stream:
                writer = csv.DictWriter(stream, fieldnames=self.row.keys())
                writer.writeheader()
                writer.writerows([self.row, self.row])
            with self.assertRaises(ValueError):
                load(path)


if __name__ == '__main__':
    unittest.main(verbosity=2)
