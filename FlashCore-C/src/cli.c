#include "ftl.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static fc_ftl f;
static uint32_t reference[FC_LBAS];
static uint8_t present[FC_LBAS];
static int number(const char *s, uint32_t max, uint32_t *out)
{
    char *end;
    unsigned long v;
    if (*s < '0' || *s > '9') return 0;
    errno = 0;
    v = strtoul(s, &end, 10);
    if (errno || *end || v > max) return 0;
    *out = (uint32_t)v;
    return 1;
}
static int check_reference(void)
{
    uint16_t l;
    for (l = 0; l < FC_LBAS; ++l) {
        uint32_t value = 0;
        fc_error e = fc_read(&f, l, &value);
        if ((present[l] && (e != FC_OK || value != reference[l])) ||
            (!present[l] && e != FC_UNMAPPED)) return 0;
    }
    return fc_check(&f) == FC_OK;
}
int main(int argc, char **argv)
{
    static char line[128];
    uint32_t op = 0;
    if (argc == 2 && strcmp(argv[1], "--ram") == 0) {
        printf("map_bytes,state_bytes,owner_bytes,data_bytes,erase_count_bytes,bad_table_bytes,ftl_bytes\n");
        printf("%zu,%zu,%zu,%zu,%zu,%zu,%zu\n", sizeof(f.map), sizeof(f.state),
               sizeof(f.owner), sizeof(f.data), sizeof(f.erase_count), sizeof(f.bad), sizeof(f));
        return ferror(stdout) ? 2 : 0;
    }
    if (argc != 1) { fputs("usage: flashcore < workload.csv | flashcore --ram\n", stderr); return 2; }
    fc_init(&f);
    puts("op,command,status,value,host_writes,physical_programs,gc_migrations,erases,write_amplification");
    while (fgets(line, sizeof(line), stdin)) {
        char *a, *b, *end;
        uint32_t x, y, value = 0;
        fc_error e;
        if (!strchr(line, '\n') && !feof(stdin)) goto malformed;
        end = line + strcspn(line, "\r\n");
        *end = '\0';
        if (line[0] == '#' || line[0] == '\0') continue;
        a = strchr(line, ',');
        if (!a) goto malformed;
        *a++ = '\0';
        b = strchr(a, ',');
        if (!b) goto malformed;
        *b++ = '\0';
        if (!number(a, UINT16_MAX, &x) || !number(b, UINT32_MAX, &y)) goto malformed;
        if (strcmp(line, "WRITE") == 0) {
            e = fc_write(&f, (uint16_t)x, y);
            if (e == FC_OK) { reference[x] = y; present[x] = 1; }
        } else if (strcmp(line, "READ") == 0) e = fc_read(&f, (uint16_t)x, &value);
        else if (strcmp(line, "TRIM") == 0) {
            e = fc_trim(&f, (uint16_t)x);
            if (e == FC_OK) present[x] = 0;
        } else if (strcmp(line, "FAULT_PROGRAM") == 0) { fc_fault(&f, y, 0); e = FC_OK; }
        else if (strcmp(line, "FAULT_ERASE") == 0) { fc_fault(&f, 0, y); e = FC_OK; }
        else if (strcmp(line, "RETIRE") == 0) e = fc_retire(&f, (uint16_t)x);
        else if (strcmp(line, "GC") == 0) e = fc_gc(&f);
        else goto malformed;
        if (!check_reference()) { fputs("reference/invariant mismatch\n", stderr); return 3; }
        printf("%" PRIu32 ",%s,%s,%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32
               ",%" PRIu32 ",%.6f\n", ++op, line, fc_name(e), value,
               f.metrics.host_writes, f.metrics.physical_programs, f.metrics.gc_migrations,
               f.metrics.erases, f.metrics.host_writes ?
               (double)f.metrics.physical_programs / f.metrics.host_writes : 0.0);
    }
    if (ferror(stdin) || ferror(stdout)) return 2;
    return check_reference() ? 0 : 3;
malformed:
    fputs("malformed CSV command (expected COMMAND,unsigned,unsigned)\n", stderr);
    return 2;
}
