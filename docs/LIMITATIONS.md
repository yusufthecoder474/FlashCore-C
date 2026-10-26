# Limitations

- Educational, volatile, single-threaded NAND/FTL simulation; no real SSD, microcontroller, UART, controller registers, interrupts, DMA or driver access was executed.
- NAND pages contain one 32-bit value. No ECC, OOB, bit transitions, partial programming, timing, disturb, retention, endurance threshold, ECC failure, read failure or real bad-block-marker discovery is modeled.
- Copy-before-invalidate protects against the implemented synchronous failure model. It is not power-loss atomicity: RAM metadata is not journaled, persistent or recoverable across reset. All state resets at initialization.
- Wear-aware allocation picks the least-erased eligible free block; it is not complete static/dynamic wear leveling and does not guarantee balanced lifetime. No endurance improvement is claimed.
- Retirement refuses live/protected blocks; no automatic live-data evacuation on program failure. Erase-failure retirement is supported. If no spare exists, the design can stop with NO_SPACE while retaining readable live data, even if a more sophisticated recovery algorithm could reclaim space.
- Corrupted/uninitialized context pointers and concurrent access are outside the API contract. `fc_check` is an invariant checker, not a hardened forensic repair engine. Calls must use initialized contexts.
- All source arrays are fixed-size; changing geometry requires code/range review and rebuilding. Current geometry is the only verified geometry. uint32_t counters may wrap if used beyond UINT32_MAX; limit experimental workloads accordingly.
- `physical_programs` counts successful programs, not all attempted NAND operations. This distinction matters for write amplification under faults.
- Host CLI/reference checks and standard I/O add overhead; timings are not SSD throughput, NAND latency or firmware deadlines. No baseline improvement claim is made.
- GCC/Linux is verified in Debug, Release and ASan+UBSan builds. Windows, MSVC and Clang are not verified. MSVC is intentionally rejected by the strict-C99 build. LeakSanitizer could not run in this environment.
- cppcheck and clang-tidy were absent; GCC -fanalyzer was used as an additional check. No claim of cppcheck/clang-tidy cleanliness.
- No C++ FlashCore ZIP was attached. CSV compatibility and C-vs-C++ performance/correctness equivalence remain pending. The normalized comparison tool itself is tested.
- No claim of production readiness, deployed firmware, independent human authorship, hardware testing or completed internship work. Study, rerun and explain the code before using the supplied bullets.
