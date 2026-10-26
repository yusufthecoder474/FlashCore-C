# Resume bullets

Use these after reproducing the runs and understanding the code. Numbers come from this package's Linux evidence. These are educational simulation/host-test claims, not hardware or production results. Do not claim independent authorship solely because the package was generated for you.

## FlashCore-C

- Implemented a 32-LBA C99 FTL in a measured 580-byte context using fixed arrays, out-of-place writes, greedy garbage collection and wear-aware free-block selection across 64 simulated pages.
- Preserved reference-model consistency across 20,000 C stress operations and 615,681 CSV rows per validation pass using Python oracles and deterministic program/erase-failure tests.
- Verified 19/19 CTest entries in Linux Debug, Release and ASan/UBSan builds, covering GC rollback at six copy positions, bad-block retirement and spare-block exhaustion.

## FirmwareKit-C

- Verified 15/15 CTest entries in Linux Debug, Release and ASan/UBSan builds for an 8-byte C99 ring buffer, CRC32, bit helpers and a bounded UART-style parser.
- Validated 10,071 responses across 11 Python command scripts by checking 10,000 randomized queue operations and 36 CRC payloads against independent Python models.

## Evidence boundaries

FlashCore's 19 entries are 17 C cases, one Python workload validator and one Python comparison-test entry (six internal tests). FirmwareKit's 15 entries are 14 C cases plus one Python validator. These are not 19 or 15 independent suites per platform; repeated configurations do not increase the unique-test count. Leak detection was disabled because LeakSanitizer could not inspect processes here; AddressSanitizer and UBSan remained enabled. Windows and hardware testing are pending. Exact CSV row counts include diagnostic READs, not only host workload operations. No performance-improvement percentage or real-SSD claim is justified.
