/*
 * getgrent_test_suite.c
 *
 * Comprehensive AIX getgrent Test Suite
 *
 * This single program:
 *   1. Creates test groups/users with configurable sizes
 *   2. Tests getgrent_r with guarded buffer monitoring
 *   3. Cleans up test groups/users
 *
 * SAFETY: Only manipulates groups/users with prefix "ztest_" to avoid
 *         any conflict with system groups or admin-created groups.
 *
 * Compile on AIX:
 *   xlc_r -g -o getgrent_test_suite getgrent_test_suite.c -ls
 * or:
 *   gcc -D_THREAD_SAFE -g -pthread -o getgrent_test_suite getgrent_test_suite.c -ls
 *
 * Usage:
 *   ./getgrent_test_suite setup <num_members>   # Create test group
 *   ./getgrent_test_suite test [buffer_size]    # Run getgrent tests
 *   ./getgrent_test_suite cleanup               # Remove test groups/users
 *   ./getgrent_test_suite all <num_members>     # Setup, test, cleanup
 *
 * Must be run as root.
 */

/*
 * Define _THREAD_SAFE to get AIX classic reentrant APIs
 * These return struct pointers rather than int (POSIX style)
 */
#define _THREAD_SAFE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <grp.h>
#include <pwd.h>
#include <usersec.h>

/*
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
 * Note: getgrent_r is NOT part of POSIX and has varying signatures.
 * For enumeration, we use non-reentrant getgrent() which is simpler.
 */

/*======================================================================
 * SAFETY CONFIGURATION
 *
 * All test entities use this prefix to ensure we NEVER touch system
 * groups or user-created groups. The prefix "ztest_" is chosen because:
 *   - Starts with 'z' so it sorts last in listings
 *   - Contains "test" to make purpose obvious
 *   - Underscore separates from real group names
 *======================================================================*/

#define TEST_PREFIX         "ztest_"
#define TEST_GROUP_NAME     "ztest_grp"
#define TEST_USER_PREFIX    "ztest_u"
#define TEST_BASE_GID       59900       /* High GID range unlikely to conflict */
#define TEST_BASE_UID       59900       /* High UID range unlikely to conflict */

/*======================================================================
 * GUARDED BUFFER IMPLEMENTATION
 *======================================================================*/

#define GUARD_MAGIC_HEAD    0xDEADBEEF
#define GUARD_MAGIC_TAIL    0xCAFEBABE
#define GUARD_FILL_BYTE     0x5A
#define BUFFER_FILL_BYTE    0xAA
#define HEAD_GUARD_SIZE     64
#define TAIL_GUARD_SIZE     256

typedef struct {
    size_t          alloc_size;
    size_t          total_size;
    unsigned char   *raw_alloc;
    unsigned char   *head_guard;
    unsigned char   *buffer;
    unsigned char   *tail_guard;
} guarded_buffer_t;

static void fill_guard_region(unsigned char *region, size_t size,
                              unsigned int magic, unsigned char fill)
{
    if (size >= sizeof(unsigned int)) {
        *(unsigned int *)region = magic;
        region += sizeof(unsigned int);
        size -= sizeof(unsigned int);
    }
    memset(region, fill, size);
}

static int check_guard_region(const unsigned char *region, size_t size,
                              unsigned int expected_magic, unsigned char expected_fill,
                              const char *region_name, const char *context)
{
    int errors = 0;
    size_t i;

    if (size >= sizeof(unsigned int)) {
        unsigned int actual_magic = *(const unsigned int *)region;
        if (actual_magic != expected_magic) {
            fprintf(stderr, "[CORRUPTION] %s: %s magic changed! "
                    "0x%08X -> 0x%08X\n",
                    context, region_name, expected_magic, actual_magic);
            errors++;
        }
        region += sizeof(unsigned int);
        size -= sizeof(unsigned int);
    }

    for (i = 0; i < size; i++) {
        if (region[i] != expected_fill) {
            if (errors < 5) {
                fprintf(stderr, "[CORRUPTION] %s: %s[%zu] = 0x%02X (expected 0x%02X)\n",
                        context, region_name, i, region[i], expected_fill);
            }
            errors++;
        }
    }

    if (errors > 5) {
        fprintf(stderr, "  ... %d more corrupted bytes in %s\n", errors - 5, region_name);
    }

    return errors;
}

static guarded_buffer_t *guarded_alloc(size_t size)
{
    guarded_buffer_t *gb = malloc(sizeof(guarded_buffer_t));
    if (!gb) return NULL;

    gb->alloc_size = size;
    gb->total_size = HEAD_GUARD_SIZE + size + TAIL_GUARD_SIZE;
    gb->raw_alloc = malloc(gb->total_size);

    if (!gb->raw_alloc) {
        free(gb);
        return NULL;
    }

    gb->head_guard = gb->raw_alloc;
    gb->buffer = gb->raw_alloc + HEAD_GUARD_SIZE;
    gb->tail_guard = gb->buffer + size;

    fill_guard_region(gb->head_guard, HEAD_GUARD_SIZE, GUARD_MAGIC_HEAD, GUARD_FILL_BYTE);
    memset(gb->buffer, BUFFER_FILL_BYTE, size);
    fill_guard_region(gb->tail_guard, TAIL_GUARD_SIZE, GUARD_MAGIC_TAIL, GUARD_FILL_BYTE);

    return gb;
}

static void guarded_free(guarded_buffer_t *gb)
{
    if (gb) {
        if (gb->raw_alloc) {
            memset(gb->raw_alloc, 0xDD, gb->total_size);
            free(gb->raw_alloc);
        }
        free(gb);
    }
}

static int guarded_check(guarded_buffer_t *gb, const char *context)
{
    int head_err = check_guard_region(gb->head_guard, HEAD_GUARD_SIZE,
                                      GUARD_MAGIC_HEAD, GUARD_FILL_BYTE,
                                      "HEAD", context);
    int tail_err = check_guard_region(gb->tail_guard, TAIL_GUARD_SIZE,
                                      GUARD_MAGIC_TAIL, GUARD_FILL_BYTE,
                                      "TAIL", context);

    if (tail_err > 0) {
        fprintf(stderr, "[CRITICAL] %s: BUFFER OVERFLOW - %d bytes corrupted past end!\n",
                context, tail_err);
    }
    if (head_err > 0) {
        fprintf(stderr, "[CRITICAL] %s: BUFFER UNDERFLOW - %d bytes corrupted before start!\n",
                context, head_err);
    }

    return (head_err == 0 && tail_err == 0) ? 0 : -1;
}

/*======================================================================
 * SAFETY CHECKS
 *======================================================================*/

static int check_root(void)
{
    if (getuid() != 0) {
        fprintf(stderr, "ERROR: This program must be run as root.\n");
        return -1;
    }
    return 0;
}

/*
 * Verify a group name is safe to manipulate (has our test prefix)
 */
static int is_safe_group(const char *name)
{
    return (strncmp(name, TEST_PREFIX, strlen(TEST_PREFIX)) == 0);
}

/*
 * Verify a user name is safe to manipulate (has our test prefix)
 */
static int is_safe_user(const char *name)
{
    return (strncmp(name, TEST_PREFIX, strlen(TEST_PREFIX)) == 0);
}

/*======================================================================
 * GROUP/USER CREATION (using AIX Security APIs)
 *======================================================================*/

/*
 * Build SEC_LIST format: "str1\0str2\0str3\0\0"
 */
static char *build_sec_list(int count, const char *prefix, int start_idx)
{
    size_t total = 0;
    int i;
    char name[64];
    char *list, *p;

    /* Calculate size */
    for (i = 0; i < count; i++) {
        snprintf(name, sizeof(name), "%s%04d", prefix, start_idx + i);
        total += strlen(name) + 1;
    }
    total += 1;  /* Double NUL terminator */

    list = malloc(total);
    if (!list) return NULL;

    p = list;
    for (i = 0; i < count; i++) {
        snprintf(name, sizeof(name), "%s%04d", prefix, start_idx + i);
        strcpy(p, name);
        p += strlen(name) + 1;
    }
    *p = '\0';  /* Double NUL */

    return list;
}

/*
 * Create a test user
 */
static int create_test_user(const char *username, uid_t uid)
{
    int ret;
    char home[128];

    if (!is_safe_user(username)) {
        fprintf(stderr, "SAFETY: Refusing to create user without test prefix: %s\n", username);
        return -1;
    }

    /* Check if already exists */
    if (getpwnam(username) != NULL) {
        return 0;  /* Already exists */
    }

    if (setuserdb(S_WRITE) != 0) {
        perror("setuserdb");
        return -1;
    }

    ret = putuserattr((char *)username, NULL, NULL, SEC_NEW);
    if (ret != 0) {
        fprintf(stderr, "putuserattr SEC_NEW failed for %s: %s\n", username, strerror(errno));
        enduserdb();
        return -1;
    }

    putuserattr((char *)username, S_ID, (void *)&uid, SEC_INT);

    snprintf(home, sizeof(home), "/tmp/%s", username);
    putuserattr((char *)username, S_HOME, (void *)home, SEC_CHAR);
    putuserattr((char *)username, S_PGRP, (void *)"staff", SEC_CHAR);
    putuserattr((char *)username, S_SHELL, (void *)"/bin/false", SEC_CHAR);

    ret = putuserattr((char *)username, NULL, NULL, SEC_COMMIT);
    enduserdb();

    return ret;
}

/*
 * Create the test group with specified number of members
 */
static int create_test_group(int num_members)
{
    int ret, i;
    gid_t gid = TEST_BASE_GID;
    char *member_list = NULL;
    char username[64];

    printf("\n=== Creating Test Group ===\n");
    printf("Group name: %s\n", TEST_GROUP_NAME);
    printf("GID:        %d\n", gid);
    printf("Members:    %d\n", num_members);

    if (!is_safe_group(TEST_GROUP_NAME)) {
        fprintf(stderr, "SAFETY: Group name doesn't have test prefix!\n");
        return -1;
    }

    /* Create test users first */
    printf("\nCreating %d test users...\n", num_members);
    for (i = 0; i < num_members; i++) {
        snprintf(username, sizeof(username), "%s%04d", TEST_USER_PREFIX, i + 1);
        if (create_test_user(username, TEST_BASE_UID + i + 1) != 0) {
            fprintf(stderr, "Failed to create user %s\n", username);
        }
        if ((i + 1) % 100 == 0) {
            printf("  Created %d users...\n", i + 1);
        }
    }
    printf("Users created.\n");

    /* Open database */
    if (setuserdb(S_WRITE) != 0) {
        perror("setuserdb");
        return -1;
    }

    /* Delete existing test group if present */
    if (getgrnam(TEST_GROUP_NAME) != NULL) {
        printf("Removing existing test group...\n");
        putgroupattr(TEST_GROUP_NAME, NULL, NULL, SEC_DELETE);
        putgroupattr(TEST_GROUP_NAME, NULL, NULL, SEC_COMMIT);
    }

    /* Create new group */
    printf("Creating group %s...\n", TEST_GROUP_NAME);
    ret = putgroupattr(TEST_GROUP_NAME, NULL, NULL, SEC_NEW);
    if (ret != 0) {
        fprintf(stderr, "putgroupattr SEC_NEW failed: %s\n", strerror(errno));
        enduserdb();
        return -1;
    }

    /* Set GID */
    ret = putgroupattr(TEST_GROUP_NAME, S_ID, (void *)&gid, SEC_INT);
    if (ret != 0) {
        fprintf(stderr, "putgroupattr S_ID failed: %s\n", strerror(errno));
        enduserdb();
        return -1;
    }

    /* Set members if any */
    if (num_members > 0) {
        printf("Adding %d members to group...\n", num_members);
        member_list = build_sec_list(num_members, TEST_USER_PREFIX, 1);
        if (member_list) {
            ret = putgroupattr(TEST_GROUP_NAME, S_USERS, (void *)member_list, SEC_LIST);
            free(member_list);
            if (ret != 0) {
                fprintf(stderr, "putgroupattr S_USERS failed: %s\n", strerror(errno));
            }
        }
    }

    /* Commit */
    ret = putgroupattr(TEST_GROUP_NAME, NULL, NULL, SEC_COMMIT);
    enduserdb();

    if (ret == 0) {
        printf("\nTest group created successfully.\n");

        /* Verify */
        struct group *grp = getgrnam(TEST_GROUP_NAME);
        if (grp) {
            int count = 0;
            if (grp->gr_mem) {
                for (char **m = grp->gr_mem; *m; m++) count++;
            }
            printf("Verification: %s (GID=%d) has %d members\n",
                   grp->gr_name, grp->gr_gid, count);
        }
    }

    return ret;
}

/*======================================================================
 * CLEANUP
 *======================================================================*/

static int cleanup_test_entities(void)
{
    int i;
    char name[64];

    printf("\n=== Cleaning Up Test Entities ===\n");

    if (setuserdb(S_WRITE) != 0) {
        perror("setuserdb");
        return -1;
    }

    /* Remove test group */
    printf("Removing test group: %s\n", TEST_GROUP_NAME);
    if (getgrnam(TEST_GROUP_NAME) != NULL) {
        putgroupattr(TEST_GROUP_NAME, NULL, NULL, SEC_DELETE);
        putgroupattr(TEST_GROUP_NAME, NULL, NULL, SEC_COMMIT);
    }

    enduserdb();

    /* Remove test users */
    printf("Removing test users (this may take a moment)...\n");
    for (i = 1; i <= 2000; i++) {  /* Up to max possible */
        snprintf(name, sizeof(name), "%s%04d", TEST_USER_PREFIX, i);

        if (getpwnam(name) == NULL) {
            continue;  /* Doesn't exist */
        }

        if (!is_safe_user(name)) {
            continue;  /* Safety check */
        }

        if (setuserdb(S_WRITE) == 0) {
            putuserattr(name, NULL, NULL, SEC_DELETE);
            putuserattr(name, NULL, NULL, SEC_COMMIT);
            enduserdb();
        }

        if (i % 100 == 0) {
            printf("  Removed %d users...\n", i);
        }
    }

    printf("Cleanup complete.\n");
    return 0;
}

/*======================================================================
 * GETGRENT TESTS WITH GUARDED BUFFERS
 *======================================================================*/

/*
 * Validate all pointers in struct group are within buffer
 */
static int validate_group_ptrs(const struct group *grp,
                               const unsigned char *buf, size_t bufsize)
{
    int errors = 0;
    char **mem;

    if (grp->gr_name) {
        if ((unsigned char *)grp->gr_name < buf ||
            (unsigned char *)grp->gr_name >= buf + bufsize) {
            fprintf(stderr, "  gr_name (%p) outside buffer!\n", grp->gr_name);
            errors++;
        }
    }

    if (grp->gr_passwd) {
        if ((unsigned char *)grp->gr_passwd < buf ||
            (unsigned char *)grp->gr_passwd >= buf + bufsize) {
            fprintf(stderr, "  gr_passwd (%p) outside buffer!\n", grp->gr_passwd);
            errors++;
        }
    }

    if (grp->gr_mem) {
        if ((unsigned char *)grp->gr_mem < buf ||
            (unsigned char *)grp->gr_mem >= buf + bufsize) {
            fprintf(stderr, "  gr_mem (%p) outside buffer!\n", grp->gr_mem);
            errors++;
        } else {
            for (mem = grp->gr_mem; *mem; mem++) {
                if ((unsigned char *)(*mem) < buf ||
                    (unsigned char *)(*mem) >= buf + bufsize) {
                    errors++;
                }
            }
        }
    }

    return errors;
}

/*
 * Aggressively read all group data (will crash if pointers invalid)
 */
static void aggressive_read(const struct group *grp)
{
    volatile int sum = 0;
    const char *p;
    char **mem;

    if (grp->gr_name) {
        for (p = grp->gr_name; *p; p++) sum += *p;
    }
    if (grp->gr_passwd) {
        for (p = grp->gr_passwd; *p; p++) sum += *p;
    }
    sum += grp->gr_gid;
    if (grp->gr_mem) {
        for (mem = grp->gr_mem; *mem; mem++) {
            for (p = *mem; *p; p++) sum += *p;
        }
    }
    (void)sum;
}

/*
 * Test getgrent enumeration (using non-reentrant getgrent)
 *
 * Note: getgrent_r is not part of POSIX and has platform-specific signatures.
 * We use the simpler getgrent() for enumeration. The returned data uses
 * static storage, so we copy what we need immediately.
 */
static void test_enumerate(size_t bufsize)
{
    struct group *grp;
    int total = 0;
    int test_group_found = 0;
    int test_group_members = 0;

    (void)bufsize;  /* Not used for non-reentrant version */

    printf("\n=== Test: Enumerate All Groups ===\n");
    printf("Using non-reentrant getgrent() for enumeration\n");
    printf("(getgrent_r has platform-specific signatures)\n");

    setgrent();

    while ((grp = getgrent()) != NULL) {
        total++;

        /* Check if this is our test group */
        if (strcmp(grp->gr_name, TEST_GROUP_NAME) == 0) {
            test_group_found = 1;
            if (grp->gr_mem) {
                for (char **m = grp->gr_mem; *m; m++) {
                    test_group_members++;
                }
            }
            printf("  Found test group: %s (GID=%d, %d members)\n",
                   grp->gr_name, grp->gr_gid, test_group_members);
        }
    }

    endgrent();

    printf("\n--- Enumeration Results ---\n");
    printf("Total groups:       %d\n", total);

    if (test_group_found) {
        printf("\nTest group %s: FOUND (%d members)\n", TEST_GROUP_NAME, test_group_members);
    } else {
        printf("\nTest group %s: NOT FOUND (run 'setup' first)\n", TEST_GROUP_NAME);
    }
}

/*
 * Test direct lookup with progressive buffer sizing
 * Uses POSIX getgrnam_r: int getgrnam_r(name, grp, buf, buflen, &result)
 */
static void test_lookup_progressive(const char *groupname)
{
    struct group grp;
    guarded_buffer_t *gb = NULL;
    struct group *result;
    int ret;
    size_t sizes[] = {32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 0};
    int i;

    printf("\n=== Test: Progressive Buffer Sizing ===\n");
    printf("Looking up: %s\n", groupname);

    for (i = 0; sizes[i] != 0; i++) {
        if (gb) guarded_free(gb);

        gb = guarded_alloc(sizes[i]);
        if (!gb) {
            perror("guarded_alloc");
            return;
        }

        memset(gb->buffer, BUFFER_FILL_BYTE, gb->alloc_size);

        printf("\nAttempt %d: buffer = %zu bytes\n", i + 1, sizes[i]);

        /* POSIX getgrnam_r: returns 0 on success, error code on failure */
        ret = getgrnam_r(groupname, &grp, (char *)gb->buffer,
                         gb->alloc_size, &result);

        /* Check guards regardless of result */
        int guard_ok = (guarded_check(gb, "lookup") == 0);

        if (ret == ERANGE) {
            printf("  Result: ERANGE (too small)\n");
            printf("  Guards: %s\n", guard_ok ? "OK" : "CORRUPTED!");
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

        /* Success */
        int members = 0;
        if (grp.gr_mem) {
            for (char **m = grp.gr_mem; *m; m++) members++;
        }

        printf("  Result: SUCCESS\n");
        printf("  Group:  %s (GID=%d)\n", grp.gr_name, grp.gr_gid);
        printf("  Members: %d\n", members);
        printf("  Guards: %s\n", guard_ok ? "OK" : "CORRUPTED!");

        /* Validate and aggressive read */
        if (validate_group_ptrs(&grp, gb->buffer, gb->alloc_size) == 0) {
            printf("  Pointers: Valid\n");
            aggressive_read(&grp);
            printf("  Data read: OK\n");
        } else {
            printf("  Pointers: INVALID!\n");
        }

        break;
    }

    if (sizes[i] == 0) {
        printf("\n[FAILED] Could not find large enough buffer!\n");
    }

    if (gb) guarded_free(gb);
}

/*
 * Test with intentionally small buffer to verify ERANGE handling
 * Uses POSIX getgrnam_r: int getgrnam_r(name, grp, buf, buflen, &result)
 */
static void test_small_buffer(const char *groupname)
{
    struct group grp;
    guarded_buffer_t *gb;
    struct group *result;
    int ret;
    size_t tiny = 64;

    printf("\n=== Test: Small Buffer (ERANGE Expected) ===\n");
    printf("Looking up: %s\n", groupname);
    printf("Buffer: %zu bytes (intentionally small)\n", tiny);

    gb = guarded_alloc(tiny);
    if (!gb) {
        perror("guarded_alloc");
        return;
    }

    memset(gb->buffer, BUFFER_FILL_BYTE, gb->alloc_size);

    /* POSIX getgrnam_r: returns 0 on success, error code on failure */
    ret = getgrnam_r(groupname, &grp, (char *)gb->buffer,
                     gb->alloc_size, &result);

    printf("\nReturn: %d (%s)\n", ret, ret ? strerror(ret) : "success");
    printf("Result: %s\n", result ? "non-NULL (unexpected!)" : "NULL");

    if (ret == ERANGE) {
        printf("\n[EXPECTED] Got ERANGE - buffer too small\n");
    }

    /* Critical: check if library overflowed despite ERANGE */
    printf("\nChecking %d-byte tail guard for overflow...\n", TAIL_GUARD_SIZE);
    if (guarded_check(gb, "small buffer") == 0) {
        printf("[GOOD] No overflow - library respected buffer boundary\n");
    } else {
        printf("[CRITICAL] Library wrote past buffer despite returning ERANGE!\n");
    }

    guarded_free(gb);
}

/*======================================================================
 * MAIN
 *======================================================================*/

static void usage(const char *prog)
{
    printf("Usage: %s <command> [options]\n", prog);
    printf("\nCommands:\n");
    printf("  setup <num_members>   Create test group with N members\n");
    printf("  test [buffer_size]    Run getgrent tests (default buffer: 4096)\n");
    printf("  cleanup               Remove all test groups and users\n");
    printf("  all <num_members>     Setup, test, and cleanup\n");
    printf("\nExamples:\n");
    printf("  %s setup 50           Create group with 50 members\n", prog);
    printf("  %s setup 500          Create group with 500 members\n", prog);
    printf("  %s test               Test with default 4096-byte buffer\n", prog);
    printf("  %s test 256           Test with 256-byte buffer (will trigger ERANGE)\n", prog);
    printf("  %s all 100            Full test cycle with 100 members\n", prog);
    printf("\nSafety:\n");
    printf("  All test entities use prefix '%s' to avoid conflicts.\n", TEST_PREFIX);
    printf("  Test GID range: %d+\n", TEST_BASE_GID);
    printf("  Test UID range: %d+\n", TEST_BASE_UID);
}

int main(int argc, char *argv[])
{
    const char *cmd;
    int num_members = 50;
    size_t bufsize = 4096;

    printf("AIX getgrent Test Suite\n");
    printf("=======================\n");
    printf("Test prefix: %s\n", TEST_PREFIX);

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    if (check_root() != 0) {
        return 1;
    }

    cmd = argv[1];

    if (strcmp(cmd, "setup") == 0) {
        if (argc > 2) {
            num_members = atoi(argv[2]);
            if (num_members < 0) num_members = 0;
            if (num_members > 1900) {
                printf("Warning: AIX limits groups to ~2000 members, capping at 1900\n");
                num_members = 1900;
            }
        }
        return create_test_group(num_members);

    } else if (strcmp(cmd, "test") == 0) {
        if (argc > 2) {
            bufsize = (size_t)atoi(argv[2]);
            if (bufsize < 32) bufsize = 32;
        }

        test_small_buffer(TEST_GROUP_NAME);
        test_lookup_progressive(TEST_GROUP_NAME);
        test_enumerate(bufsize);
        return 0;

    } else if (strcmp(cmd, "cleanup") == 0) {
        return cleanup_test_entities();

    } else if (strcmp(cmd, "all") == 0) {
        if (argc > 2) {
            num_members = atoi(argv[2]);
            if (num_members < 0) num_members = 0;
            if (num_members > 1900) num_members = 1900;
        }

        printf("\n>>> Phase 1: Setup\n");
        if (create_test_group(num_members) != 0) {
            fprintf(stderr, "Setup failed!\n");
            return 1;
        }

        printf("\n>>> Phase 2: Test\n");
        test_small_buffer(TEST_GROUP_NAME);
        test_lookup_progressive(TEST_GROUP_NAME);
        test_enumerate(4096);

        printf("\n>>> Phase 3: Cleanup\n");
        cleanup_test_entities();

        printf("\n>>> All phases complete\n");
        return 0;

    } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "-h") == 0) {
        usage(argv[0]);
        return 0;

    } else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        usage(argv[0]);
        return 1;
    }
}
