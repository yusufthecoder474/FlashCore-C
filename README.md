# FlashCore-C

A small C99 NAND/FTL simulator for learning storage firmware algorithms. It runs on a host computer, not an SSD controller. It uses fixed arrays and explicit status codes; no application malloc/free, VLA, recursion, framework or external C library.

## Start in VS Code

Open this folder. Read `include/ftl.h`, `src/ftl.c`, then `tests/test_ftl.c`. Build from the integrated terminal. Python 3 standard library only; no pip dependencies for validation.

Linux prerequisites: GCC, CMake 3.16+, make, Python 3.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --verbose
ctest --test-dir build --output-on-failure
./build/flashcore --ram
./build/flashcore < workloads/demo.csv
python3 scripts/validate.py --exe build/flashcore --out validation
```

Windows 11 PowerShell prerequisites: Python 3, CMake, MSYS2 UCRT64 GCC and mingw32-make installed. These commands are provided but **Windows has not been run**. Adjust the MSYS2 path if installed elsewhere. Use GCC, not MSVC (this project requires strict C99).

```powershell
$env:Path = "C:\msys64\ucrt64\bin;" + $env:Path
cmake -S . -B build-win -G "MinGW Makefiles" -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Debug
cmake --build build-win --verbose
ctest --test-dir build-win --output-on-failure
.\build-win\flashcore.exe --ram
Get-Content .\workloads\demo.csv | .\build-win\flashcore.exe
python scripts/validate.py --exe build-win/flashcore.exe --out validation
```

CMake's Python interpreter must match a working Python 3 on PATH; pass `-DPython3_EXECUTABLE=<full-python-path>` if needed. CTest failures produce a nonzero status.

## Design and error handling

Geometry in `ftl.h`: 8 blocks x 8 pages = 64 physical pages; 32 logical pages; a page stores one uint32_t test value. There is one active block and one reserved erased spare. All persistent state lives in a statically allocated `fc_ftl`. Fixed arrays use uint8_t states, uint16_t indices and uint32_t values/counters. Scalar local variables use bounded stack space.

WRITE programs a fresh FREE page before invalidating the old page and changing the map. READ looks up the map; TRIM invalidates a mapped page and is idempotent. A failed program consumes an INVALID page and leaves the old logical value intact. Reads do not alter metrics.

When the active block fills, allocation chooses the eligible erased block with the lowest erase count (block index breaks ties). The spare is excluded. If no block is available, greedy GC selects the non-active, non-spare, good block with most INVALID pages; index breaks ties. A victim needs at least one invalid page, ensuring room for a subsequent host write.

GC stages successful copies as INVALID pages in the spare. Only after every copy succeeds does it commit mappings, mark destination pages VALID and invalidate sources. It then erases the victim and exchanges block roles. A failed copy leaves source mappings intact and erases the staged destination. If cleanup erase fails, the destination is retired and another erased spare is selected if available. The returned PROGRAM error has precedence over a secondary cleanup ERASE error; the bad-block table records the latter.

An erase failure after copy commit retires the old victim; data already resides in the new active block. GC returns ERASE even though migration committed. The triggering host write has not happened and may be retried. If no spare remains, remaining active space can be used, then NO_SPACE is returned. The simulator deliberately has no complex spare-recovery policy.

`fc_retire` marks a non-active/non-spare block bad only if it has no VALID pages. Otherwise BUSY protects live data. Retirement is idempotent. Allocation and GC both exclude bad blocks. Program failure does not automatically retire a block containing other live data.

`fc_fault(f, program_n, erase_n)` injects a one-shot failure on the Nth attempt; zero disables that fault. Calling it resets both countdowns. `fc_check` validates map/owner/page-state consistency and block-role invariants. Public context pointers must be initialized and non-null; READ separately checks its output pointer. There is no recovery from arbitrary memory corruption.

## CLI contract

Input: `COMMAND,unsigned,unsigned`, decimal only; `#` comments and blank lines are allowed. No CSV header. Commands:

| Command | Second field | Third field |
|---|---|---|
| WRITE | LBA | uint32_t value |
| READ / TRIM | LBA | ignored, use 0 |
| GC | ignored, use 0 | ignored, use 0 |
| RETIRE | block | ignored, use 0 |
| FAULT_PROGRAM / FAULT_ERASE | ignored, use 0 | countdown |

Output is a per-command CSV with operation number, command, status, read value and cumulative metrics. Non-READ value fields are zero. Status names: OK, RANGE, UNMAPPED, NO_SPACE, PROGRAM, ERASE, BUSY, ARGUMENT, CORRUPT. Expected simulated failures appear in CSV; a completed run exits 0. Exit 2 means malformed input/usage/I/O failure, and 3 means a reference/invariant mismatch. Scripts must check both exit codes and row statuses. Lines are bounded by a 128-byte static buffer; LF/CRLF are supported.

## Metrics and RAM

- host_writes: successful host WRITE commands, not attempts or TRIMs.
- physical_programs: successful page programs, including GC copies later discarded during rollback; failed program attempts are excluded.
- gc_migrations: copies committed by GC; staged copies that roll back are excluded.
- erases: successful erases, including rollback cleanup; failed erases are excluded.
- write_amplification = physical_programs / host_writes; CSV uses 0.0 when host_writes is zero (a reporting convention, mathematically undefined).

This differs from an attempted-program metric under faults. Never compare to another simulator without reconciling definitions. Counters are 32-bit; workloads must stay below overflow. This is not a long-running production counter design.

`--ram` computes sizeof values on the executing compiler. Linux GCC measured: map 32 x 2 = 64 bytes; page states 64 x 1 = 64; owners 64 x 2 = 128; page values 64 x 4 = 256; erase counts 8 x 4 = 32; bad table 8 x 1 = 8. Arrays total 552 bytes; full FTL context is 580 bytes including counters and block roles. The CLI additionally has 160 bytes of reference arrays and a 128-byte line buffer, plus scalar stack and hosted-library overhead. The 580-byte claim is not total process RAM. No application heap allocation does not mean libc's stdio internals never allocate.

## Verification and comparison

CTest: 17 C cases + workload validation + comparison-script suite = 19 entries. The assert macro remains active in Release. The Python comparison suite has 6 cases. Workload validation runs 15 scenarios twice, checks byte-identical CSV and validates 615,681 rows per pass, including reads of all 32 LBAs after every original operation. C also checks a separate array model after each CLI command. Unit stress adds 20,000 deterministic operations with all-LBA checks. Seven malformed inputs must exit 2.

```bash
cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build-san
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --test-dir build-san --output-on-failure
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DENABLE_ANALYZER=ON
cmake --build build-release --verbose
ctest --test-dir build-release --output-on-failure
```

Leak detection is disabled above because this host blocks LeakSanitizer's process inspection; ASan/UBSan remain enabled. On an unrestricted Linux machine, also run with `detect_leaks=1`. Sanitizer builds use non-PIE executables for predictable sanitizer mappings and are restricted to Linux in CMake. Windows sanitizer verification is pending. `-fanalyzer` is an additional GCC check, not cppcheck/clang-tidy.

No C++ FlashCore ZIP was attached. This is an independent implementation and its CSV format is not asserted compatible with that unseen project. `scripts/compare_results.py` compares normalized summary CSVs:

```bash
python3 scripts/compare_results.py validation/summary.csv other-summary.csv --report comparison.md
python3 scripts/test_compare.py
```

Required columns: workload, seed, blocks, pages_per_block, lbas, workload_sha256, metric_version, host_writes, physical_programs, gc_migrations, erases, write_amplification. Keys must be unique. WA tolerance is 0.000001; other selected fields must match. Geometry, workload hash and metric-version differences are reported rather than silently compared. Additional timing columns are ignored. Exit 0 = match; 1 = mismatch or invalid input. Different GC policies may legitimately differ; a metric mismatch alone does not prove data corruption.

For a future C++ comparison, first inspect its real parser and counters, add an explicit adapter, feed identical expanded workload CSVs/seeds/geometry, and normalize its summary to this schema. Do not rename columns without checking semantics. Actual C++ comparison remains pending.

See `docs/EVIDENCE.md`, `docs/LIMITATIONS.md`, `docs/INTERVIEW_GUIDE.md`, `docs/RESUME_BULLETS.md` and the archived `docs/evidence/` logs/CSV files. Validation wall timings include process startup, CSV and exhaustive reference checks; they are not SSD IOPS/latency.
