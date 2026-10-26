# FlashCore-C validation

Executable: `/workspace/scratch/3acfd9351244/FlashCore-C/build-sanitize/flashcore`
Workloads passed: 15/15; two identical runs each.
Checked CSV rows per pass: 615681. Malformed-input cases: 7.
Each original command is followed by reads of all 32 LBAs against a Python model.
CLI also compares its C reference array and structural invariants after every command.
Timings in runs.json include process startup, CSV I/O and oracle work in C; not SSD performance.

Failures: none

| Workload | Seed | Host writes | Programs | GC copies | Erases | WA |
|---|---:|---:|---:|---:|---:|---:|
| uniform | 7 | 1589 | 2160 | 571 | 263 | 1.359346 |
| uniform | 19 | 1598 | 2146 | 548 | 262 | 1.342929 |
| uniform | 41 | 1604 | 2204 | 600 | 269 | 1.374065 |
| hotcold | 7 | 1609 | 2477 | 868 | 303 | 1.539466 |
| hotcold | 19 | 1615 | 2454 | 839 | 300 | 1.519505 |
| hotcold | 41 | 1600 | 2504 | 904 | 306 | 1.565000 |
| trim_mix | 7 | 1295 | 1490 | 195 | 180 | 1.150579 |
| trim_mix | 19 | 1329 | 1530 | 201 | 185 | 1.151242 |
| trim_mix | 41 | 1345 | 1564 | 219 | 189 | 1.162825 |
| sequential | 0 | 256 | 256 | 0 | 25 | 1.000000 |
| program_fault | 0 | 2 | 2 | 0 | 0 | 1.000000 |
| gc_rollback | 0 | 40 | 47 | 6 | 2 | 1.175000 |
| erase_fault | 0 | 40 | 46 | 6 | 0 | 1.150000 |
| retirement | 0 | 16 | 16 | 0 | 0 | 1.000000 |
| bounds | 0 | 0 | 0 | 0 | 0 | 0.000000 |
