# Interview guide

These 15 questions refer to the packaged C code, not to real-hardware results. Use the source names while explaining; run the tests yourself before an interview.

1. **What problem does FlashCore-C solve?**
   It maps 32 logical addresses to 64 simulated physical pages. `fc_write` implements out-of-place updates because the model permits programs only into FREE pages. It demonstrates the software bookkeeping of an FTL, not a real NAND driver.

2. **Why static allocation and fixed-width types?**
   `fc_ftl` owns fixed arrays and the CLI declares one static instance. There is no allocator failure or heap fragmentation in application code. uint16_t addresses include the UINT16_MAX unmapped sentinel; uint8_t holds page states; uint32_t holds values/counters. The tradeoff is a compile-time capacity and a need to review widths when geometry changes. Scalar locals still use stack space.

3. **How much RAM does the map use?**
   `sizeof(f.map)` is 32 x sizeof(uint16_t) = 64 bytes on the verified target. Page-state bytes are 64, owner bytes 128, payload bytes 256, erase-count bytes 32 and bad-table bytes 8. Arrays total 552; `sizeof(fc_ftl)` is 580 including metrics and roles. The CLI oracle/buffer and runtime are additional, so 580 is not total process RAM.

4. **What happens during an overwrite or TRIM?**
   `fc_write` programs a FREE page, then invalidates the old mapping and commits the new one. A failed program leaves the previous value authoritative and consumes the attempted page as INVALID. `fc_trim` invalidates the mapped page and clears the map. Repeated TRIM is OK; reading an unmapped LBA returns UNMAPPED.

5. **How does GC choose a victim and use the spare?**
   `fc_gc` selects the good block with most INVALID pages, excluding active and spare. Ties favor the lowest index. It needs at least one invalid page. The spare is an erased destination that guarantees room for all live pages in a victim. If no eligible victim/spare exists, GC returns NO_SPACE.

6. **How does GC avoid data loss on a copy failure?**
   It stages copies as INVALID without changing source mappings. Only after all programs succeed does it commit destination mappings and invalidate sources. On failure, it erases the staged destination; even if cleanup erase fails and retires that destination, sources remain intact. `gc_fault_positions` fails each of the six live-copy positions in its fixture and checks rollback. This does not provide power-loss safety.

7. **What if the erase fails after copy commit?**
   The victim is retired and live data stays in the new active block. `fc_gc` returns ERASE and finds a different spare if possible. The host write which triggered GC has not executed. If no spare remains, remaining active slots can be used, then writes can return NO_SPACE. The `exhausted` test checks this intentionally conservative behavior.

8. **How does the bad-block table work?**
   `bad[block]` is a uint8_t flag checked by allocation and victim selection. An erase failure sets it. `fc_retire` also allows deterministic retirement, but refuses active/spare blocks and blocks with VALID data using BUSY. No factory bad-block markers are read; the table is volatile and modeled.

9. **Is wear-aware allocation full wear leveling?**
   No. `free_block` chooses the eligible fully erased block with the lowest erase count, breaking ties by index. It does not migrate cold data to balance wear or enforce endurance limits. `wear_choice` supplies synthetic erase history to isolate this decision; it is not an SSD lifetime experiment.

10. **How is write amplification calculated, and how do you validate correctness?**
    WA is successful physical programs divided by successful host writes. Committed GC copies and successful copies later rolled back both count as physical programs; only committed copies count as migrations. A no-host-write run reports 0 by convention. The C CLI checks an array model after each command, while Python inserts 32 reads after every original operation and checks those independently. A separate 20,000-operation C stress case checks every LBA each time. Debug, Release and sanitizer summaries matched; no C++ source comparison was possible.

11. **How would you debug the injected mapping/GC failure?**
    Run `./build/test_ftl gc_rollback` or `gc_fault_positions`; inspect `program`, `fc_gc` and the map snapshot. A staging program failure must not modify source mappings. Follow the explicit status and check `fc_check`; then inspect the raw CSV and Python reference result. This is reproduction/validation of injected failures, not evidence of discovering a production defect. The LeakSanitizer failure in the logs came from restricted process inspection; it was resolved for ASan/UBSan execution by disabling leak detection, not by changing FTL logic.

12. **What would change on real hardware?**
    Replace RAM page access with NAND controller operations; add ECC/OOB management, factory/runtime bad-block handling, flash timing/status checks, persistent mapping/journaling and power-cut recovery. Add concurrency, DMA/cache-coherency rules and explicit ISR/task ownership. Validate stack/linker RAM, endurance and timing on the actual board. None of these hardware capabilities is claimed here.

13. **How does the ring buffer handle full, empty and wraparound?**
    `fw_push` and `fw_pop` use head/tail modulo 8 and a count. All eight slots are usable. Full rejects the newest value without losing queued data; empty leaves the caller output untouched. The C wrap test executes 1,000 push/pop pairs and Python compares 10,000 randomized queue operations with a list. The ring is not safe for concurrent ISR/task use without a synchronization design.

14. **What does CRC32 do, and how are bit shifts kept safe?**
    `fw_crc32` uses reflected polynomial 0xEDB88320 with all-ones initialization/final XOR. `123456789` produces CBF43926 and 36 payloads are checked against Python zlib. CRC is not authentication. Bit helpers guard indices below 32; field extraction handles width 32 separately so it never shifts a 32-bit value by 32. Invalid bit operations return the original value; invalid fields return zero, as documented.

15. **How does the UART-style state machine recover from malformed input?**
    `fw_feed` has COLLECT/DISCARD states with a 40-byte buffer. Overflow, binary data or misplaced CR discards input until LF, emits one ERR FRAME and starts fresh. CR immediately before LF is supported. `fw_finish` rejects an incomplete final command without executing it. Unit tests exercise resynchronization; Python checks exact responses. This is stdin-based protocol parsing, not a hardware UART implementation.
