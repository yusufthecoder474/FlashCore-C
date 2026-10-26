#ifndef FTL_H
#define FTL_H
#include <stddef.h>
#include <stdint.h>
#define FC_BLOCKS 8u
#define FC_PAGES 8u
#define FC_LBAS 32u
#define FC_TOTAL (FC_BLOCKS * FC_PAGES)
#define FC_NONE UINT16_MAX
#define FC_FREE 0u
#define FC_VALID 1u
#define FC_INVALID 2u
typedef enum { FC_OK, FC_RANGE, FC_UNMAPPED, FC_NO_SPACE, FC_PROGRAM,
               FC_ERASE, FC_BUSY, FC_ARGUMENT, FC_CORRUPT } fc_error;
typedef struct {
    uint32_t host_writes, physical_programs, gc_migrations, erases;
} fc_metrics;
typedef struct {
    uint16_t map[FC_LBAS], owner[FC_TOTAL];
    uint32_t data[FC_TOTAL], erase_count[FC_BLOCKS];
    uint8_t state[FC_TOTAL], bad[FC_BLOCKS];
    uint16_t active, spare;
    uint32_t program_countdown, erase_countdown;
    fc_metrics metrics;
} fc_ftl;
void fc_init(fc_ftl *f);
fc_error fc_write(fc_ftl *f, uint16_t lba, uint32_t value);
fc_error fc_read(const fc_ftl *f, uint16_t lba, uint32_t *value);
fc_error fc_trim(fc_ftl *f, uint16_t lba);
fc_error fc_gc(fc_ftl *f);
fc_error fc_retire(fc_ftl *f, uint16_t block);
/* 1 = next attempt; 0 disables. One-shot faults; counters count attempts. */
void fc_fault(fc_ftl *f, uint32_t program_n, uint32_t erase_n);
fc_error fc_check(const fc_ftl *f);
const char *fc_name(fc_error e);
#endif
