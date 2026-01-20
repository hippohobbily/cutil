/*
 * getgrent_buffer_stress_test.c
 *
 * AIX Buffer Stress Test for getgrent_r and related functions
 *
 * This program deliberately tests buffer size edge cases:
 * 1. Uses intentionally small buffers to trigger ERANGE
 * 2. Demonstrates proper retry-with-larger-buffer handling
 * 3. Includes code that reads/writes across entire buffer range
 *    (would segfault if buffer size assumptions are violated)
 * 4. Validates buffer integrity after each operation
 *
 * Compile on AIX:
 *   xlc_r -g -o buffer_stress getgrent_buffer_stress_test.c
 * or:
 *   gcc -D_THREAD_SAFE -g -pthread -o buffer_stress getgrent_buffer_stress_test.c
 *
 * Run after setting up test groups with create_test_groups.sh
 */

#define _THREAD_SAFE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <grp.h>
#include <signal.h>
#include <setjmp.h>

/*----------------------------------------------------------------------
 * AIX POSIX Reentrant APIs (declared in <grp.h>):
 *
 *   int getgrnam_r(const char *name, struct group *grp,
 *                  char *buffer, size_t buflen, struct group **result);
 *   int getgrgid_r(gid_t gid, struct group *grp,
 *                  char *buffer, size_t buflen, struct group **result);
 *
 * These return 0 on success, error code on failure.
 * result points to grp on success, NULL if not found.
 *
 * Note: getgrent_r is NOT part of POSIX - we use getgrent() for enumeration.
 *----------------------------------------------------------------------*/

/*----------------------------------------------------------------------
 * Configuration
 *----------------------------------------------------------------------*/

/* Buffer sizes for testing - intentionally small to trigger failures */
#define TINY_BUFFER     64      /* Will fail for almost any group */
#define SMALL_BUFFER    256     /* Will fail for groups with several members */
#define MEDIUM_BUFFER   1024    /* May fail for large groups */
#define LARGE_BUFFER    4096    /* Should work for most groups */
#define HUGE_BUFFER     65536   /* Should work for all groups */

/* Magic values for buffer integrity checking */
#define GUARD_MAGIC_HEAD    0xDEADBEEF
#define GUARD_MAGIC_TAIL    0xCAFEBABE
#define GUARD_FILL_BYTE     0x5A          /* Fill pattern for guard regions */
#define BUFFER_FILL_BYTE    0xAA          /* Fill pattern for buffer area */

/* Guard region sizes */
#define HEAD_GUARD_SIZE     64            /* Bytes before buffer */
#define TAIL_GUARD_SIZE     256           /* Bytes after buffer - larger to catch overflows */

/*----------------------------------------------------------------------
 * Buffer Guard Structure
 *
 * Wraps the actual buffer with guard regions to detect overflow/underflow
 *
 * Memory layout:
 * ┌────────────────────────────────────────────────────────────────────┐
 * │ HEAD GUARD (64 bytes)                                              │
 * │   [0xDEADBEEF] [0x5A 0x5A 0x5A ... 60 bytes of 0x5A]              │
 * ├────────────────────────────────────────────────────────────────────┤
 * │ USER BUFFER (requested size)                                       │
 * │   [0xAA 0xAA 0xAA ... filled with 0xAA initially]                 │
 * ├────────────────────────────────────────────────────────────────────┤
 * │ TAIL GUARD (256 bytes) - watched for corruption                    │
 * │   [0xCAFEBABE] [0x5A 0x5A 0x5A ... 252 bytes of 0x5A]             │
 * └────────────────────────────────────────────────────────────────────┘
 *----------------------------------------------------------------------*/

typedef struct {
    /* Metadata */
    size_t          alloc_size;         /* Requested buffer size */
    size_t          total_size;         /* Total allocated (head + buffer + tail) */

    /* Raw allocation */
    unsigned char   *raw_alloc;         /* Original malloc pointer */

    /* Guard regions */
    unsigned char   *head_guard;        /* Points to head guard region */
    unsigned char   *buffer;            /* Points to user buffer area */
    unsigned char   *tail_guard;        /* Points to tail guard region */

    /* Expected values at guard boundaries */
    unsigned int    head_magic;         /* Should be GUARD_MAGIC_HEAD */
    unsigned int    tail_magic;         /* Should be GUARD_MAGIC_TAIL */
} guarded_buffer_t;

/*
 * Fill a memory region with a repeating pattern and magic value at start
 */
static void fill_guard_region(unsigned char *region, size_t size,
                              unsigned int magic, unsigned char fill)
{
    /* Place magic value at start */
    if (size >= sizeof(unsigned int)) {
        *(unsigned int *)region = magic;
        region += sizeof(unsigned int);
        size -= sizeof(unsigned int);
    }

    /* Fill rest with pattern */
    memset(region, fill, size);
}

/*
 * Verify a guard region is intact
 * Returns number of corrupted bytes (0 = OK)
 */
static int check_guard_region(const unsigned char *region, size_t size,
                              unsigned int expected_magic, unsigned char expected_fill,
                              const char *region_name, const char *context)
{
    int errors = 0;
    size_t i;
    const unsigned char *p;

    /* Check magic value */
    if (size >= sizeof(unsigned int)) {
        unsigned int actual_magic = *(const unsigned int *)region;
        if (actual_magic != expected_magic) {
            fprintf(stderr, "[CORRUPTION] %s: %s magic overwritten! "
                    "Expected 0x%08X, got 0x%08X\n",
                    context, region_name, expected_magic, actual_magic);
            errors++;
        }
        region += sizeof(unsigned int);
        size -= sizeof(unsigned int);
    }

    /* Check fill pattern byte-by-byte */
    p = region;
    for (i = 0; i < size; i++) {
        if (p[i] != expected_fill) {
            if (errors == 0) {
                /* First corruption - print header */
                fprintf(stderr, "[CORRUPTION] %s: %s fill pattern corrupted!\n",
                        context, region_name);
            }
            if (errors < 10) {
                /* Print first 10 corrupted bytes */
                fprintf(stderr, "  Offset %zu: expected 0x%02X, got 0x%02X\n",
                        i, expected_fill, p[i]);
            }
            errors++;
        }
    }

    if (errors > 10) {
        fprintf(stderr, "  ... and %d more corrupted bytes\n", errors - 10);
    }

    return errors;
}

/*
 * Allocate a guarded buffer
 */
static guarded_buffer_t *alloc_guarded_buffer(size_t size)
{
    guarded_buffer_t *gb;
    size_t total;

    gb = malloc(sizeof(guarded_buffer_t));
    if (gb == NULL) return NULL;

    /* Calculate total size needed */
    total = HEAD_GUARD_SIZE + size + TAIL_GUARD_SIZE;

    /* Allocate raw memory block */
    gb->raw_alloc = malloc(total);
    if (gb->raw_alloc == NULL) {
        free(gb);
        return NULL;
    }

    /* Set up pointers */
    gb->alloc_size = size;
    gb->total_size = total;
    gb->head_guard = gb->raw_alloc;
    gb->buffer = gb->raw_alloc + HEAD_GUARD_SIZE;
    gb->tail_guard = gb->buffer + size;
    gb->head_magic = GUARD_MAGIC_HEAD;
    gb->tail_magic = GUARD_MAGIC_TAIL;

    /* Initialize head guard region */
    fill_guard_region(gb->head_guard, HEAD_GUARD_SIZE,
                      GUARD_MAGIC_HEAD, GUARD_FILL_BYTE);

    /* Initialize user buffer with known pattern */
    memset(gb->buffer, BUFFER_FILL_BYTE, size);

    /* Initialize tail guard region */
    fill_guard_region(gb->tail_guard, TAIL_GUARD_SIZE,
                      GUARD_MAGIC_TAIL, GUARD_FILL_BYTE);

    return gb;
}

/*
 * Free a guarded buffer
 */
static void free_guarded_buffer(guarded_buffer_t *gb)
{
    if (gb != NULL) {
        if (gb->raw_alloc != NULL) {
            /* Optionally wipe memory before freeing */
            memset(gb->raw_alloc, 0xDD, gb->total_size);
            free(gb->raw_alloc);
        }
        free(gb);
    }
}

/*
 * Validate buffer integrity - checks all guard regions
 * Returns 0 if OK, -1 if corrupted
 */
static int validate_buffer_integrity(guarded_buffer_t *gb, const char *context)
{
    int head_errors, tail_errors;

    /* Check head guard (underflow detection) */
    head_errors = check_guard_region(gb->head_guard, HEAD_GUARD_SIZE,
                                     GUARD_MAGIC_HEAD, GUARD_FILL_BYTE,
                                     "HEAD GUARD", context);

    /* Check tail guard (overflow detection) */
    tail_errors = check_guard_region(gb->tail_guard, TAIL_GUARD_SIZE,
                                     GUARD_MAGIC_TAIL, GUARD_FILL_BYTE,
                                     "TAIL GUARD", context);

    if (tail_errors > 0) {
        fprintf(stderr, "[CRITICAL] %s: BUFFER OVERFLOW DETECTED - "
                "%d bytes written past buffer end!\n",
                context, tail_errors);
    }

    if (head_errors > 0) {
        fprintf(stderr, "[CRITICAL] %s: BUFFER UNDERFLOW DETECTED - "
                "%d bytes written before buffer start!\n",
                context, head_errors);
    }

    return (head_errors == 0 && tail_errors == 0) ? 0 : -1;
}

/*
 * Print guard region status (for debugging)
 */
static void print_guard_status(guarded_buffer_t *gb)
{
    printf("Guard Buffer Status:\n");
    printf("  Total allocation:  %zu bytes\n", gb->total_size);
    printf("  Head guard:        %p (%d bytes)\n",
           (void *)gb->head_guard, HEAD_GUARD_SIZE);
    printf("  User buffer:       %p (%zu bytes)\n",
           (void *)gb->buffer, gb->alloc_size);
    printf("  Tail guard:        %p (%d bytes)\n",
           (void *)gb->tail_guard, TAIL_GUARD_SIZE);
    printf("  Head magic:        0x%08X (expect 0x%08X)\n",
           *(unsigned int *)gb->head_guard, GUARD_MAGIC_HEAD);
    printf("  Tail magic:        0x%08X (expect 0x%08X)\n",
           *(unsigned int *)gb->tail_guard, GUARD_MAGIC_TAIL);
}

/*----------------------------------------------------------------------
 * Aggressive Buffer Validation
 *
 * These functions read/write across the ENTIRE buffer range.
 * If the buffer is smaller than expected, these WILL segfault.
 *----------------------------------------------------------------------*/

/*
 * Write pattern across entire buffer
 * This WILL segfault if buffer is smaller than 'size'
 */
static void fill_buffer_aggressively(unsigned char *buffer, size_t size)
{
    size_t i;
    volatile unsigned char *p = buffer;

    /* Touch every byte - will segfault if buffer too small */
    for (i = 0; i < size; i++) {
        p[i] = (unsigned char)(i & 0xFF);
    }

    /* Verify by reading back */
    for (i = 0; i < size; i++) {
        if (p[i] != (unsigned char)(i & 0xFF)) {
            fprintf(stderr, "[FATAL] Buffer verification failed at offset %zu\n", i);
            abort();
        }
    }
}

/*
 * After getgrent_r populates buffer, validate ALL pointers point within buffer
 * This catches cases where library wrote outside our buffer
 */
static int validate_group_pointers(const struct group *grp,
                                   const unsigned char *buffer,
                                   size_t bufsize,
                                   const char *context)
{
    int errors = 0;
    char **mem;

    /* Check gr_name pointer */
    if (grp->gr_name != NULL) {
        if ((unsigned char *)grp->gr_name < buffer ||
            (unsigned char *)grp->gr_name >= buffer + bufsize) {
            fprintf(stderr, "[CORRUPTION] %s: gr_name (%p) outside buffer [%p-%p]\n",
                    context, grp->gr_name, buffer, buffer + bufsize);
            errors++;
        } else {
            /* Validate string is null-terminated within buffer */
            size_t max_len = bufsize - ((unsigned char *)grp->gr_name - buffer);
            size_t len = strnlen(grp->gr_name, max_len);
            if (len == max_len) {
                fprintf(stderr, "[CORRUPTION] %s: gr_name not null-terminated\n", context);
                errors++;
            }
        }
    }

    /* Check gr_passwd pointer */
    if (grp->gr_passwd != NULL) {
        if ((unsigned char *)grp->gr_passwd < buffer ||
            (unsigned char *)grp->gr_passwd >= buffer + bufsize) {
            fprintf(stderr, "[CORRUPTION] %s: gr_passwd (%p) outside buffer\n",
                    context, grp->gr_passwd);
            errors++;
        }
    }

    /* Check gr_mem array and each member string */
    if (grp->gr_mem != NULL) {
        if ((unsigned char *)grp->gr_mem < buffer ||
            (unsigned char *)grp->gr_mem >= buffer + bufsize) {
            fprintf(stderr, "[CORRUPTION] %s: gr_mem array (%p) outside buffer\n",
                    context, grp->gr_mem);
            errors++;
        } else {
            /* Check each member pointer */
            for (mem = grp->gr_mem; *mem != NULL; mem++) {
                /* Check the pointer itself is in buffer */
                if ((unsigned char *)mem >= buffer + bufsize) {
                    fprintf(stderr, "[CORRUPTION] %s: gr_mem[%ld] pointer outside buffer\n",
                            context, (long)(mem - grp->gr_mem));
                    errors++;
                    break;
                }

                /* Check the string it points to */
                if ((unsigned char *)(*mem) < buffer ||
                    (unsigned char *)(*mem) >= buffer + bufsize) {
                    fprintf(stderr, "[CORRUPTION] %s: gr_mem[%ld] value (%p) outside buffer\n",
                            context, (long)(mem - grp->gr_mem), *mem);
                    errors++;
                }
            }
        }
    }

    return errors;
}

/*
 * Aggressively read all data from struct group
 * Will segfault if any pointers are invalid
 */
static void read_group_aggressively(const struct group *grp)
{
    volatile char c;
    volatile int sum = 0;
    char **mem;
    const char *p;

    /* Read every character of gr_name */
    if (grp->gr_name != NULL) {
        for (p = grp->gr_name; *p != '\0'; p++) {
            c = *p;
            sum += c;
        }
    }

    /* Read every character of gr_passwd */
    if (grp->gr_passwd != NULL) {
        for (p = grp->gr_passwd; *p != '\0'; p++) {
            c = *p;
            sum += c;
        }
    }

    /* Read the gid */
    sum += grp->gr_gid;

    /* Read every member string completely */
    if (grp->gr_mem != NULL) {
        for (mem = grp->gr_mem; *mem != NULL; mem++) {
            for (p = *mem; *p != '\0'; p++) {
                c = *p;
                sum += c;
            }
        }
    }

    /* Use sum to prevent optimizer from removing reads */
    if (sum == -99999999) {
        printf("Unlikely\n");
    }
}

/*----------------------------------------------------------------------
 * Test Functions
 *----------------------------------------------------------------------*/

/*
 * Test 1: Deliberately use tiny buffer - expect ERANGE
 * Uses POSIX getgrnam_r: int getgrnam_r(name, grp, buf, buflen, &result)
 */
static void test_tiny_buffer(const char *groupname)
{
    struct group grp;
    guarded_buffer_t *gb;
    struct group *result;
    int ret;

    printf("\n");
    printf("============================================================\n");
    printf("TEST: Tiny Buffer (%d bytes) - Expecting ERANGE\n", TINY_BUFFER);
    printf("============================================================\n");
    printf("Looking up group: %s\n", groupname);

    gb = alloc_guarded_buffer(TINY_BUFFER);
    if (gb == NULL) {
        perror("alloc_guarded_buffer");
        return;
    }

    /* Pre-fill buffer to detect how much was written */
    memset(gb->buffer, BUFFER_FILL_BYTE, gb->alloc_size);

    /* POSIX getgrnam_r: returns 0 on success, error code on failure */
    ret = getgrnam_r(groupname, &grp, (char *)gb->buffer,
                     gb->alloc_size, &result);

    printf("Return: %d (%s)\n", ret, ret ? strerror(ret) : "success");
    printf("Result: %s\n", result == NULL ? "NULL" : "non-NULL");

    if (ret == ERANGE) {
        printf("[EXPECTED] Got ERANGE - buffer too small\n");
    } else if (ret == 0 && result != NULL) {
        printf("[UNEXPECTED] Succeeded with tiny buffer!\n");
        printf("  Group: %s, GID: %d\n", grp.gr_name, grp.gr_gid);

        /* Validate pointers are within buffer */
        if (validate_group_pointers(&grp, gb->buffer, gb->alloc_size, "tiny") != 0) {
            printf("[CRITICAL] Pointer validation failed!\n");
        }
    }

    /* Check for buffer overflow */
    if (validate_buffer_integrity(gb, "tiny buffer test") != 0) {
        printf("[CRITICAL] Buffer overflow detected!\n");
    }

    free_guarded_buffer(gb);
}

/*
 * Test 2: Progressive buffer sizing with retry
 * Uses POSIX getgrnam_r: int getgrnam_r(name, grp, buf, buflen, &result)
 */
static void test_progressive_sizing(const char *groupname)
{
    struct group grp;
    guarded_buffer_t *gb = NULL;
    struct group *result;
    int ret;
    size_t sizes[] = {64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 0};
    int i;
    int attempt = 0;

    printf("\n");
    printf("============================================================\n");
    printf("TEST: Progressive Buffer Sizing with Retry\n");
    printf("============================================================\n");
    printf("Looking up group: %s\n", groupname);

    for (i = 0; sizes[i] != 0; i++) {
        attempt++;

        if (gb != NULL) {
            free_guarded_buffer(gb);
        }

        gb = alloc_guarded_buffer(sizes[i]);
        if (gb == NULL) {
            perror("alloc_guarded_buffer");
            return;
        }

        /* Pre-fill to detect modifications */
        memset(gb->buffer, BUFFER_FILL_BYTE, gb->alloc_size);

        printf("\nAttempt %d: buffer size = %zu bytes\n", attempt, sizes[i]);

        /* POSIX getgrnam_r: returns 0 on success, error code on failure */
        ret = getgrnam_r(groupname, &grp, (char *)gb->buffer,
                         gb->alloc_size, &result);

        if (ret == ERANGE) {
            printf("  Result: ERANGE - buffer too small, will retry\n");

            /* Check that library didn't overflow despite returning ERANGE */
            if (validate_buffer_integrity(gb, "ERANGE check") != 0) {
                printf("  [CRITICAL] Buffer overflowed even though ERANGE returned!\n");
            }
            continue;
        }

        if (ret != 0) {
            printf("  Result: Error %d - %s\n", ret, strerror(ret));
            break;
        }

        if (result == NULL) {
            printf("  Result: Group not found\n");
            break;
        }

        /* Success! */
        printf("  Result: SUCCESS\n");
        printf("  Group: %s\n", grp.gr_name);
        printf("  GID:   %d\n", grp.gr_gid);

        /* Count members */
        int member_count = 0;
        if (grp.gr_mem != NULL) {
            char **mem;
            for (mem = grp.gr_mem; *mem != NULL; mem++) {
                member_count++;
            }
        }
        printf("  Members: %d\n", member_count);

        /* Validate buffer integrity */
        if (validate_buffer_integrity(gb, "success check") != 0) {
            printf("  [CRITICAL] Buffer corruption detected!\n");
        }

        /* Validate all pointers */
        if (validate_group_pointers(&grp, gb->buffer, gb->alloc_size, "success") != 0) {
            printf("  [CRITICAL] Pointer validation failed!\n");
        }

        /* Aggressively read all data - will segfault if pointers bad */
        printf("  Performing aggressive read of all group data...\n");
        read_group_aggressively(&grp);
        printf("  Aggressive read completed successfully\n");

        break;
    }

    if (sizes[i] == 0) {
        printf("\n[FAILED] Could not find buffer size large enough!\n");
    }

    if (gb != NULL) {
        free_guarded_buffer(gb);
    }
}

/*
 * Test 3: Enumerate all groups, track statistics
 *
 * Note: getgrent_r is not POSIX and has platform-specific signatures.
 * We use the simpler non-reentrant getgrent() for enumeration.
 */
static void test_enumeration_with_stats(size_t buffer_size)
{
    struct group *grp;
    int total = 0;
    int max_members = 0;
    char max_members_group[256] = "";

    (void)buffer_size;  /* Not used for non-reentrant version */

    printf("\n");
    printf("============================================================\n");
    printf("TEST: Enumerate All Groups (using non-reentrant getgrent)\n");
    printf("============================================================\n");
    printf("Note: getgrent_r has platform-specific signatures on AIX\n");

    setgrent();

    while ((grp = getgrent()) != NULL) {
        total++;

        /* Count members */
        int member_count = 0;
        if (grp->gr_mem != NULL) {
            char **mem;
            for (mem = grp->gr_mem; *mem != NULL; mem++) {
                member_count++;
            }
        }

        if (member_count > max_members) {
            max_members = member_count;
            strncpy(max_members_group, grp->gr_name, sizeof(max_members_group) - 1);
        }

        /* Show progress for groups with many members */
        if (member_count > 10) {
            printf("  %s: %d members\n", grp->gr_name, member_count);
        }
    }

    endgrent();

    printf("\n");
    printf("Results:\n");
    printf("  Total groups processed: %d\n", total);
    printf("  Largest group:          %s (%d members)\n",
           max_members_group[0] ? max_members_group : "(none)", max_members);
}

/*
 * Test 4: Demonstrate what happens with wrong buffer size assumption
 *
 * This simulates a bug where code assumes a larger buffer than allocated
 * The fill_buffer_aggressively will SEGFAULT if the buffer is too small
 */
static void test_buffer_size_assumption_violation(void)
{
    printf("\n");
    printf("============================================================\n");
    printf("TEST: Buffer Size Assumption Violation\n");
    printf("============================================================\n");
    printf("This test allocates a small buffer but then accesses it\n");
    printf("as if it were larger. This WILL crash if guards are removed.\n\n");

    /* Allocate small buffer */
    size_t actual_size = 256;
    size_t assumed_size = 1024;  /* Bug: code thinks buffer is larger */

    guarded_buffer_t *gb = alloc_guarded_buffer(actual_size);
    if (gb == NULL) {
        perror("alloc_guarded_buffer");
        return;
    }

    printf("Allocated:    %zu bytes\n", actual_size);
    printf("Code assumes: %zu bytes\n", assumed_size);

    /* SAFE: Access within actual bounds */
    printf("\nSafe access (within %zu bytes)...\n", actual_size);
    fill_buffer_aggressively(gb->buffer, actual_size);
    printf("Safe access completed.\n");

    /* Check integrity */
    if (validate_buffer_integrity(gb, "safe access") != 0) {
        printf("[CRITICAL] Corruption after safe access!\n");
    } else {
        printf("Buffer integrity OK after safe access.\n");
    }

    /*
     * DANGEROUS: If we uncommented the following line, it would access
     * beyond the allocated buffer and likely SEGFAULT:
     *
     * fill_buffer_aggressively(gb->buffer, assumed_size);  // WOULD CRASH!
     *
     * This simulates the bug where getgrent_r is given wrong buffer size
     */
    printf("\n[SKIPPED] Dangerous access (would crash/corrupt)\n");
    printf("          Uncomment code to see actual segfault\n");

#if 0
    /* UNCOMMENT TO SEE CRASH */
    printf("\nDangerous access (beyond %zu bytes)...\n", actual_size);
    fill_buffer_aggressively(gb->buffer, assumed_size);  /* WILL CRASH! */
#endif

    free_guarded_buffer(gb);
}

/*
 * Test 5: Verify getgrnam_r doesn't write beyond stated buffer size
 * Uses POSIX getgrnam_r: int getgrnam_r(name, grp, buf, buflen, &result)
 */
static void test_overflow_detection(const char *large_group)
{
    struct group grp;
    guarded_buffer_t *gb;
    struct group *result;
    int ret;

    printf("\n");
    printf("============================================================\n");
    printf("TEST: Overflow Detection for Large Group\n");
    printf("============================================================\n");
    printf("Testing group: %s\n", large_group);
    printf("Using buffer just barely large enough to potentially overflow\n\n");

    /* Use a buffer size that might be just barely enough */
    size_t test_size = 512;

    gb = alloc_guarded_buffer(test_size);
    if (gb == NULL) {
        perror("alloc_guarded_buffer");
        return;
    }

    /* Show guard buffer layout */
    print_guard_status(gb);
    printf("\n");

    /* Pre-fill user buffer area */
    memset(gb->buffer, BUFFER_FILL_BYTE, gb->alloc_size);

    printf("Calling getgrnam_r with buffer size %zu...\n", test_size);

    /* POSIX getgrnam_r: returns 0 on success, error code on failure */
    ret = getgrnam_r(large_group, &grp, (char *)gb->buffer,
                     gb->alloc_size, &result);

    printf("\nAfter getgrnam_r:\n");
    printf("  Return: %d (%s)\n", ret, ret ? strerror(ret) : "success");
    printf("  Result: %s\n", result ? "non-NULL" : "NULL");

    /* Full guard region validation */
    printf("\nValidating guard regions (checking all %d tail guard bytes)...\n",
           TAIL_GUARD_SIZE);

    int integrity = validate_buffer_integrity(gb, "overflow test");

    if (integrity != 0) {
        printf("\n[CRITICAL] BUFFER CORRUPTION DETECTED!\n");
        printf("  Library wrote outside the designated buffer area!\n");
        print_guard_status(gb);
    } else if (ret == ERANGE) {
        printf("\n[GOOD] Library correctly returned ERANGE without overflow\n");
        printf("  All %d bytes of tail guard region intact\n", TAIL_GUARD_SIZE);
    } else if (ret == 0 && result != NULL) {
        printf("\n[OK] Operation succeeded within buffer bounds\n");
        printf("  All guard regions intact\n");

        /* Verify data integrity */
        printf("\nValidating group data pointers...\n");
        int ptr_errors = validate_group_pointers(&grp, gb->buffer, gb->alloc_size, "overflow test");
        if (ptr_errors == 0) {
            printf("All pointers valid within buffer\n");
        } else {
            printf("[ERROR] %d pointer validation errors\n", ptr_errors);
        }
    }

    free_guarded_buffer(gb);
}

/*----------------------------------------------------------------------
 * Main
 *----------------------------------------------------------------------*/

static void usage(const char *prog)
{
    printf("Usage: %s [test] [groupname]\n", prog);
    printf("\nTests:\n");
    printf("  tiny       - Test with tiny buffer (64 bytes)\n");
    printf("  progressive - Test progressive buffer sizing\n");
    printf("  enum-small  - Enumerate all groups with small buffer\n");
    printf("  enum-large  - Enumerate all groups with large buffer\n");
    printf("  assumption  - Test buffer size assumption violation\n");
    printf("  overflow    - Test overflow detection\n");
    printf("  all         - Run all tests\n");
    printf("\nDefault groupname: tgrp_medium (from create_test_groups.sh)\n");
}

int main(int argc, char *argv[])
{
    const char *test = "all";
    const char *groupname = "tgrp_medium";  /* Default test group */
    const char *large_group = "tgrp_large";  /* For overflow test */

    printf("AIX getgrent_r Buffer Stress Test\n");
    printf("==================================\n");

    if (argc > 1) {
        test = argv[1];
    }
    if (argc > 2) {
        groupname = argv[2];
    }
    if (argc > 3) {
        large_group = argv[3];
    }

    if (strcmp(test, "help") == 0 || strcmp(test, "-h") == 0) {
        usage(argv[0]);
        return 0;
    }

    printf("Test group:  %s\n", groupname);
    printf("Large group: %s\n", large_group);

    if (strcmp(test, "tiny") == 0 || strcmp(test, "all") == 0) {
        test_tiny_buffer(groupname);
    }

    if (strcmp(test, "progressive") == 0 || strcmp(test, "all") == 0) {
        test_progressive_sizing(groupname);
    }

    if (strcmp(test, "enum-small") == 0 || strcmp(test, "all") == 0) {
        test_enumeration_with_stats(SMALL_BUFFER);
    }

    if (strcmp(test, "enum-large") == 0 || strcmp(test, "all") == 0) {
        test_enumeration_with_stats(HUGE_BUFFER);
    }

    if (strcmp(test, "assumption") == 0 || strcmp(test, "all") == 0) {
        test_buffer_size_assumption_violation();
    }

    if (strcmp(test, "overflow") == 0 || strcmp(test, "all") == 0) {
        test_overflow_detection(large_group);
    }

    printf("\n");
    printf("============================================================\n");
    printf("All tests completed\n");
    printf("============================================================\n");

    return 0;
}
