#include "ftl.h"
#include <string.h>
static uint16_t base(uint16_t b) { return (uint16_t)(b * FC_PAGES); }
static uint16_t count(const fc_ftl *f, uint16_t b, uint8_t state)
{
    uint16_t p, n = 0;
    for (p = base(b); p < base(b) + FC_PAGES; ++p)
        if (f->state[p] == state) ++n;
    return n;
}
static uint16_t free_block(const fc_ftl *f)
{
    uint16_t b, best = FC_NONE;
    for (b = 0; b < FC_BLOCKS; ++b)
        if (!f->bad[b] && b != f->active && b != f->spare &&
            count(f, b, FC_FREE) == FC_PAGES &&
            (best == FC_NONE || f->erase_count[b] < f->erase_count[best])) best = b;
    return best;
}
static int fails(uint32_t *n)
{
    if (*n == 0) return 0;
    --*n;
    return *n == 0;
}
static fc_error program(fc_ftl *f, uint16_t p, uint16_t lba, uint32_t value)
{
    if (f->bad[p / FC_PAGES] || f->state[p] != FC_FREE) return FC_CORRUPT;
    /* Failed programs consume a page; old mapping remains authoritative. */
    f->state[p] = FC_INVALID;
    if (fails(&f->program_countdown)) return FC_PROGRAM;
    f->owner[p] = lba;
    f->data[p] = value;
    ++f->metrics.physical_programs;
    return FC_OK;
}
static fc_error erase(fc_ftl *f, uint16_t b)
{
    uint16_t p;
    if (count(f, b, FC_VALID) != 0) return FC_BUSY;
    if (fails(&f->erase_countdown)) {
        f->bad[b] = 1;
        return FC_ERASE;
    }
    for (p = base(b); p < base(b) + FC_PAGES; ++p) {
        f->state[p] = FC_FREE;
        f->owner[p] = FC_NONE;
        f->data[p] = 0;
    }
    ++f->erase_count[b];
    ++f->metrics.erases;
    return FC_OK;
}
void fc_init(fc_ftl *f)
{
    uint16_t i;
    memset(f, 0, sizeof(*f));
    for (i = 0; i < FC_LBAS; ++i) f->map[i] = FC_NONE;
    for (i = 0; i < FC_TOTAL; ++i) f->owner[i] = FC_NONE;
    f->active = 0;
    f->spare = FC_BLOCKS - 1u;
}
void fc_fault(fc_ftl *f, uint32_t program_n, uint32_t erase_n)
{
    f->program_countdown = program_n;
    f->erase_countdown = erase_n;
}
fc_error fc_read(const fc_ftl *f, uint16_t lba, uint32_t *value)
{
    if (!value) return FC_ARGUMENT;
    if (lba >= FC_LBAS) return FC_RANGE;
    if (f->map[lba] == FC_NONE) return FC_UNMAPPED;
    *value = f->data[f->map[lba]];
    return FC_OK;
}
fc_error fc_trim(fc_ftl *f, uint16_t lba)
{
    if (lba >= FC_LBAS) return FC_RANGE;
    if (f->map[lba] != FC_NONE) f->state[f->map[lba]] = FC_INVALID;
    f->map[lba] = FC_NONE;
    return FC_OK;
}
fc_error fc_gc(fc_ftl *f)
{
    uint16_t b, victim = FC_NONE, invalid = 0, src, dst, dest, n = 0;
    fc_error e;
    if (f->spare == FC_NONE) return FC_NO_SPACE;
    dest = f->spare;
    for (b = 0; b < FC_BLOCKS; ++b) {
        uint16_t k = count(f, b, FC_INVALID);
        if (!f->bad[b] && b != f->active && b != dest && k > invalid) {
            invalid = k;
            victim = b;
        }
    }
    if (victim == FC_NONE) return FC_NO_SPACE;
    if (count(f, dest, FC_FREE) != FC_PAGES) return FC_CORRUPT;
    dst = base(dest);
    /* Stage all copies without changing source states or mappings. */
    for (src = base(victim); src < base(victim) + FC_PAGES; ++src) {
        if (f->state[src] != FC_VALID) continue;
        e = program(f, dst, f->owner[src], f->data[src]);
        if (e != FC_OK) {
            /* Destination contains only uncommitted INVALID pages. */
            if (erase(f, dest) != FC_OK) {
                f->spare = FC_NONE;
                f->spare = free_block(f);
            }
            return e;
        }
        ++dst;
        ++n;
    }
    /* Commit only after every copy succeeds; source erase follows commit. */
    for (dst = base(dest); dst < base(dest) + n; ++dst) {
        uint16_t lba = f->owner[dst];
        f->state[f->map[lba]] = FC_INVALID;
        f->state[dst] = FC_VALID;
        f->map[lba] = dst;
    }
    f->metrics.gc_migrations += n;
    f->active = dest;
    f->spare = FC_NONE;
    e = erase(f, victim);
    f->spare = e == FC_OK ? victim : free_block(f);
    return e;
}
fc_error fc_write(fc_ftl *f, uint16_t lba, uint32_t value)
{
    uint16_t p, b;
    fc_error e;
    if (lba >= FC_LBAS) return FC_RANGE;
    if (count(f, f->active, FC_FREE) == 0) {
        b = free_block(f);
        if (b != FC_NONE) f->active = b;
        else {
            e = fc_gc(f);
            if (e != FC_OK) return e;
        }
    }
    for (p = base(f->active); p < base(f->active) + FC_PAGES; ++p)
        if (f->state[p] == FC_FREE) break;
    if (p == base(f->active) + FC_PAGES) return FC_NO_SPACE;
    e = program(f, p, lba, value);
    if (e != FC_OK) return e;
    if (f->map[lba] != FC_NONE) f->state[f->map[lba]] = FC_INVALID;
    f->map[lba] = p;
    f->state[p] = FC_VALID;
    ++f->metrics.host_writes;
    return FC_OK;
}
fc_error fc_retire(fc_ftl *f, uint16_t b)
{
    if (b >= FC_BLOCKS) return FC_RANGE;
    if (b == f->active || b == f->spare || count(f, b, FC_VALID)) return FC_BUSY;
    f->bad[b] = 1;
    return FC_OK;
}
fc_error fc_check(const fc_ftl *f)
{
    uint16_t l, p, b;
    if (f->active >= FC_BLOCKS || f->bad[f->active]) return FC_CORRUPT;
    if (f->spare != FC_NONE && (f->spare >= FC_BLOCKS ||
        f->spare == f->active || f->bad[f->spare] ||
        count(f, f->spare, FC_FREE) != FC_PAGES)) return FC_CORRUPT;
    for (l = 0; l < FC_LBAS; ++l) {
        p = f->map[l];
        if (p != FC_NONE && (p >= FC_TOTAL || f->state[p] != FC_VALID ||
            f->owner[p] != l || f->bad[p / FC_PAGES])) return FC_CORRUPT;
    }
    for (p = 0; p < FC_TOTAL; ++p) {
        b = (uint16_t)(p / FC_PAGES);
        if (f->state[p] > FC_INVALID) return FC_CORRUPT;
        if (f->state[p] == FC_VALID && (f->bad[b] || f->owner[p] >= FC_LBAS ||
            f->map[f->owner[p]] != p)) return FC_CORRUPT;
    }
    return FC_OK;
}
const char *fc_name(fc_error e)
{
    static const char *const names[] = {"OK", "RANGE", "UNMAPPED", "NO_SPACE",
        "PROGRAM", "ERASE", "BUSY", "ARGUMENT", "CORRUPT"};
    return (unsigned)e < sizeof(names) / sizeof(names[0]) ? names[e] : "UNKNOWN";
}
