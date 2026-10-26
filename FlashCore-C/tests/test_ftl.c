#include "ftl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)
static fc_ftl f;
static uint16_t snapshot[FC_LBAS];
static uint32_t ref[FC_LBAS];
static uint8_t exists[FC_LBAS];
static int fixture(void)
{
    uint16_t i;
    fc_init(&f);
    for (i = 0; i < FC_LBAS; ++i) CHECK(fc_write(&f, i, i) == FC_OK);
    for (i = 0; i < FC_LBAS; i += 4) CHECK(fc_write(&f, i, i + 100u) == FC_OK);
    CHECK(f.active == 4);
    return 0;
}
static int initial(void)
{
    uint32_t v = 99;
    fc_init(&f);
    CHECK(fc_check(&f) == FC_OK);
    CHECK(fc_read(&f, 0, &v) == FC_UNMAPPED && v == 99);
    CHECK(f.spare == 7 && f.active == 0);
    return 0;
}
static int bounds(void)
{
    uint32_t v;
    fc_init(&f);
    CHECK(fc_write(&f, FC_LBAS, 1) == FC_RANGE);
    CHECK(fc_read(&f, UINT16_MAX, &v) == FC_RANGE);
    CHECK(fc_read(&f, 0, NULL) == FC_ARGUMENT);
    CHECK(fc_trim(&f, FC_LBAS) == FC_RANGE);
    CHECK(fc_retire(&f, FC_BLOCKS) == FC_RANGE);
    CHECK(strcmp(fc_name((fc_error)99), "UNKNOWN") == 0);
    return 0;
}
static int overwrite(void)
{
    uint32_t v;
    uint16_t old;
    fc_init(&f);
    CHECK(fc_write(&f, 1, 10) == FC_OK);
    old = f.map[1];
    CHECK(fc_write(&f, 1, UINT32_MAX) == FC_OK);
    CHECK(f.map[1] != old && f.state[old] == FC_INVALID);
    CHECK(fc_read(&f, 1, &v) == FC_OK && v == UINT32_MAX);
    CHECK(f.metrics.host_writes == 2 && f.metrics.physical_programs == 2);
    return 0;
}
static int trim(void)
{
    uint32_t v;
    fc_init(&f);
    CHECK(fc_write(&f, 3, 42) == FC_OK);
    CHECK(fc_trim(&f, 3) == FC_OK && fc_trim(&f, 3) == FC_OK);
    CHECK(fc_read(&f, 3, &v) == FC_UNMAPPED);
    CHECK(fc_write(&f, 3, 7) == FC_OK);
    CHECK(fc_read(&f, 3, &v) == FC_OK && v == 7);
    return 0;
}
static int program_fault(void)
{
    uint32_t v;
    fc_init(&f);
    CHECK(fc_write(&f, 2, 4) == FC_OK);
    fc_fault(&f, 1, 0);
    CHECK(fc_write(&f, 2, 5) == FC_PROGRAM);
    CHECK(fc_read(&f, 2, &v) == FC_OK && v == 4);
    CHECK(f.metrics.host_writes == 1 && f.metrics.physical_programs == 1);
    CHECK(fc_write(&f, 2, 6) == FC_OK);
    return 0;
}
static int delayed_fault(void)
{
    fc_init(&f);
    fc_fault(&f, 2, 0);
    CHECK(fc_write(&f, 0, 1) == FC_OK);
    CHECK(fc_write(&f, 1, 2) == FC_PROGRAM);
    CHECK(fc_write(&f, 1, 2) == FC_OK);
    return 0;
}
static int gc_copy(void)
{
    uint16_t i;
    uint32_t v;
    CHECK(fixture() == 0);
    CHECK(fc_gc(&f) == FC_OK);
    CHECK(f.active == 7 && f.spare == 0);
    CHECK(f.metrics.gc_migrations == 6 && f.metrics.erases == 1);
    CHECK(f.metrics.physical_programs == 46 && f.erase_count[0] == 1);
    for (i = 0; i < FC_LBAS; ++i) {
        CHECK(fc_read(&f, i, &v) == FC_OK);
        CHECK(v == (uint32_t)(i % 4 == 0 ? i + 100 : i));
    }
    return 0;
}
static int gc_rollback(void)
{
    CHECK(fixture() == 0);
    memcpy(snapshot, f.map, sizeof(snapshot));
    fc_fault(&f, 2, 0);
    CHECK(fc_gc(&f) == FC_PROGRAM);
    CHECK(memcmp(snapshot, f.map, sizeof(snapshot)) == 0);
    CHECK(f.active == 4 && f.spare == 7 && f.metrics.gc_migrations == 0);
    CHECK(f.metrics.physical_programs == 41 && f.metrics.erases == 1);
    CHECK(fc_gc(&f) == FC_OK);
    return 0;
}
static int gc_fault_positions(void)
{
    uint32_t position;
    for (position = 1; position <= 6; ++position) {
        CHECK(fixture() == 0);
        memcpy(snapshot, f.map, sizeof(snapshot));
        fc_fault(&f, position, 0);
        CHECK(fc_gc(&f) == FC_PROGRAM);
        CHECK(memcmp(snapshot, f.map, sizeof(snapshot)) == 0);
        CHECK(f.metrics.physical_programs == 40 + position - 1);
        CHECK(fc_check(&f) == FC_OK);
        CHECK(fc_gc(&f) == FC_OK);
    }
    return 0;
}
static int erase_fault(void)
{
    CHECK(fixture() == 0);
    fc_fault(&f, 0, 1);
    CHECK(fc_gc(&f) == FC_ERASE);
    CHECK(f.bad[0] && f.active == 7 && f.spare == 5);
    CHECK(f.metrics.gc_migrations == 6 && f.metrics.erases == 0);
    return 0;
}
static int double_fault(void)
{
    CHECK(fixture() == 0);
    memcpy(snapshot, f.map, sizeof(snapshot));
    fc_fault(&f, 2, 1);
    CHECK(fc_gc(&f) == FC_PROGRAM);
    CHECK(f.bad[7] && f.spare == 5);
    CHECK(memcmp(snapshot, f.map, sizeof(snapshot)) == 0);
    CHECK(fc_gc(&f) == FC_OK);
    return 0;
}
static int retirement(void)
{
    fc_init(&f);
    CHECK(fc_retire(&f, 0) == FC_BUSY && fc_retire(&f, 7) == FC_BUSY);
    CHECK(fc_retire(&f, 1) == FC_OK && fc_retire(&f, 1) == FC_OK);
    { uint16_t i; for (i = 0; i < 9; ++i) CHECK(fc_write(&f, i, i) == FC_OK); }
    CHECK(f.active == 2);
    CHECK(fc_retire(&f, 0) == FC_BUSY);
    return 0;
}
static int wear_choice(void)
{
    uint16_t i;
    fc_init(&f);
    /* Synthetic erase history isolates the allocation decision. */
    f.erase_count[1] = 9;
    f.erase_count[2] = 5;
    f.erase_count[3] = 1;
    f.erase_count[4] = 6;
    f.erase_count[5] = 7;
    f.erase_count[6] = 8;
    for (i = 0; i < 9; ++i) CHECK(fc_write(&f, i, i) == FC_OK);
    CHECK(f.active == 3 && f.spare == 7);
    return 0;
}
static int exclude_active(void)
{
    fc_init(&f);
    CHECK(fc_write(&f, 0, 1) == FC_OK && fc_write(&f, 0, 2) == FC_OK);
    CHECK(fc_gc(&f) == FC_NO_SPACE && f.active == 0);
    return 0;
}
static int exhausted(void)
{
    CHECK(fixture() == 0);
    CHECK(fc_retire(&f, 5) == FC_OK && fc_retire(&f, 6) == FC_OK);
    fc_fault(&f, 0, 1);
    CHECK(fc_gc(&f) == FC_ERASE && f.spare == FC_NONE);
    CHECK(fc_write(&f, 0, 9) == FC_OK && fc_write(&f, 0, 10) == FC_OK);
    CHECK(fc_write(&f, 0, 11) == FC_NO_SPACE);
    return 0;
}
static int corruption(void)
{
    fc_init(&f);
    f.map[0] = FC_TOTAL;
    CHECK(fc_check(&f) == FC_CORRUPT);
    fc_init(&f);
    f.spare = f.active;
    CHECK(fc_check(&f) == FC_CORRUPT);
    fc_init(&f);
    f.state[0] = FC_VALID;
    CHECK(fc_check(&f) == FC_CORRUPT);
    fc_init(&f);
    return 0;
}
static int stress(void)
{
    uint32_t seed = 17, i, v, l;
    fc_init(&f);
    memset(exists, 0, sizeof(exists));
    for (i = 0; i < 20000; ++i) {
        seed = seed * UINT32_C(1664525) + UINT32_C(1013904223);
        l = (seed >> 16) % FC_LBAS;
        if (i % 11 == 0) { CHECK(fc_trim(&f, (uint16_t)l) == FC_OK); exists[l] = 0; }
        else {
            CHECK(fc_write(&f, (uint16_t)l, seed) == FC_OK);
            ref[l] = seed; exists[l] = 1;
        }
        for (l = 0; l < FC_LBAS; ++l) {
            fc_error e = fc_read(&f, (uint16_t)l, &v);
            CHECK(exists[l] ? e == FC_OK && v == ref[l] : e == FC_UNMAPPED);
        }
        CHECK(fc_check(&f) == FC_OK);
    }
    CHECK(f.metrics.gc_migrations > 0 && f.metrics.erases > 0);
    return 0;
}
int main(int argc, char **argv)
{
    static const struct { const char *name; int (*run)(void); } tests[] = {
        {"initial", initial}, {"bounds", bounds}, {"overwrite", overwrite}, {"trim", trim},
        {"program_fault", program_fault}, {"delayed_fault", delayed_fault}, {"gc_copy", gc_copy},
        {"gc_rollback", gc_rollback}, {"gc_fault_positions", gc_fault_positions}, {"erase_fault", erase_fault}, {"double_fault", double_fault},
        {"retirement", retirement}, {"wear_choice", wear_choice}, {"exclude_active", exclude_active},
        {"exhausted", exhausted}, {"corruption", corruption}, {"stress", stress}
    };
    size_t i;
    if (argc != 2) return 2;
    for (i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i)
        if (strcmp(argv[1], tests[i].name) == 0) {
            int result = tests[i].run();
            if (!result && fc_check(&f) != FC_OK) return 1;
            if (!result) printf("PASS %s\n", argv[1]);
            return result;
        }
    return 2;
}
