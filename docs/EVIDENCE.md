# FlashCore-C verification evidence

Date: 26 October 2026. These results were executed in the supplied Linux workspace, not inferred from the earlier C++ projects.

## Environment and setup

- Platform: `Linux-6.18.55-x86_64-with-glibc2.39`
- Compiler: GCC 13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04), x86_64 Linux.
- Python: 3.12.14; scripts use only the standard library.
- CMake/CTest: 4.4.4, installed with `python3 -m pip install cmake` after initial inspection found neither command on PATH. Binaries used: `/root/.local/bin/cmake` and `/root/.local/bin/ctest`.
- make and GCC were already available. Clang, cppcheck and clang-tidy were not available. No Windows toolchain was executed.
- `docs/evidence/environment.json` stores the exact compiler/CMake version strings and tool availability.

## Commands and outcomes

Commands below ran from the parent of the two project folders. Full argv, exit codes and log names are in `docs/evidence/commands.json`; it includes both projects for traceability. `--verbose` build logs show actual `-std=c99 -Wall -Wextra -Werror -pedantic -Wvla` flags.

```bash
/root/.local/bin/cmake -S FlashCore-C -B FlashCore-C/build-debug -DCMAKE_BUILD_TYPE=Debug
/root/.local/bin/cmake --build FlashCore-C/build-debug --verbose
/root/.local/bin/ctest --test-dir FlashCore-C/build-debug --output-on-failure
/root/.local/bin/cmake -S FlashCore-C -B FlashCore-C/build-sanitize -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
/root/.local/bin/cmake --build FlashCore-C/build-sanitize --verbose
/root/.local/bin/cmake -S FlashCore-C -B FlashCore-C/build-release -DCMAKE_BUILD_TYPE=Release -DENABLE_ANALYZER=ON
/root/.local/bin/cmake --build FlashCore-C/build-release --verbose
/root/.local/bin/ctest --test-dir FlashCore-C/build-release --output-on-failure
```

| Configuration | CTest result | Skipped | Outcome |
|---|---:|---:|---|
| GCC Debug | 19/19 | 0 | All passed |
| GCC Release, -O3 -DNDEBUG, -fanalyzer | 19/19 | 0 | All passed; no analyzer/compiler diagnostics |
| GCC ASan+UBSan, leak detection enabled | 1/19 | 0 | Environment failure; LeakSanitizer cannot inspect processes |
| GCC ASan+UBSan, leak detection disabled | 19/19 | 0 | All passed; no ASan/UBSan findings |

There are 17 named plain-C cases; the remaining CTest entries invoke Python. Counts refer to the final suite, not a sum across configurations. Initial pre-expansion tests also passed; their original logs are retained. Tests use a custom CHECK macro, so Release does not disable assertions.

## Sanitizer failure and bounded workaround

Initial sanitizer execution used `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` and `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. CTest returned 8. Logs contain `LeakSanitizer has encountered a fatal error` and `/proc/.../task` inspection errors with a ptrace-related hint. These are infrastructure failures, not clean sanitizer passes. Original failed logs are retained as `flash-sanitize-ctest.log`.

The same binaries were rerun successfully with:

```bash
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 /root/.local/bin/ctest --test-dir FlashCore-C/build-sanitize --output-on-failure
```

This command exited 0; exact env/argv are in `sanitizer-rerun.json`; results in `flash-sanitize-no-lsan-ctest.log`. ASan and UBSan remained active. LeakSanitizer is pending on an unrestricted host. No attempt was made to bypass process restrictions.

## Validation artifacts

`docs/evidence/validation-debug/` contains generated inputs, observed outputs and the report. Release and sanitized reports/summary records are retained separately. JSON records include subprocess exit codes. Workload/protocol errors expected by the tests are not test-suite failures.

- 15 workloads (three patterns x three seeds plus six focused scenarios), each run twice with byte-identical output.
- 18,657 original operations expanded to 615,681 checked rows per pass by inserting 32 diagnostic reads after every operation. These counts are not SSD I/O performance figures.
- Independent Python model and C array model both checked; 20,000 additional C stress operations check all LBAs and invariants.
- Program-failure rollback checked at all six copy positions in the GC fixture. Tests also cover cleanup erase failure, post-commit erase failure, retirement, bounds and exhaustion.
- Seven malformed CLI inputs required exit 2. RAM query required exit 0 and measured 580-byte context, 64-byte map and 64-byte state array.
- Comparison script's six unittest cases passed. CLI comparison of Debug vs Release and Debug vs ASan/UBSan summaries returned exit 0, zero mismatches. Reports are included. No C++ results were provided or compared.
- Exact successful comparison commands from the parent directory:

```bash
python3 FlashCore-C/scripts/compare_results.py FlashCore-C/build-debug/validation/summary.csv FlashCore-C/build-release/validation/summary.csv --report evidence/debug-release-comparison.md
python3 FlashCore-C/scripts/compare_results.py FlashCore-C/build-debug/validation/summary.csv FlashCore-C/build-sanitize/validation/summary.csv --report evidence/debug-sanitizer-comparison.md
```

## Pending checks

Windows 11 and Clang execution; actual MCU/SSD/UART hardware; LeakSanitizer on an unrestricted host; cppcheck/clang-tidy if installed. README commands are instructions, not evidence of platform execution. Source inspection found no application calls to malloc/calloc/realloc/free and no VLA; -Wvla and strict warning builds passed. This is not whole-process allocator tracing.
