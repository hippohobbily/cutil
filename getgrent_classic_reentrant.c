/*
 * getgrent_classic_reentrant.c
 *
 * AIX POSIX Reentrant Group Database Access Example
 *
 * Demonstrates the POSIX reentrant interface for group database access.
 * AIX uses POSIX-style signatures (not Solaris-style classic reentrant).
 *
 * Compile on AIX:
 *   xlc_r -o getgrent_classic getgrent_classic_reentrant.c
 * or:
 *   xlc -D_THREAD_SAFE -lpthread -o getgrent_classic getgrent_classic_reentrant.c
 * or with GCC:
 *   gcc -D_THREAD_SAFE -pthread -o getgrent_classic getgrent_classic_reentrant.c
 */

#define _THREAD_SAFE    /* Required for reentrant declarations on AIX */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <grp.h>

/*
 * AIX POSIX Reentrant APIs (declared in <grp.h>):
 *
 *   int getgrnam_r(const char *name, struct group *grp,
 *                  char *buffer, size_t bufsize, struct group **result);
 *   int getgrgid_r(gid_t gid, struct group *grp,
 *                  char *buffer, size_t bufsize, struct group **result);
 *
 * Return value:
 *   - Success: 0, result points to grp
 *   - Not found: 0, result is NULL
 *   - Error: error code (e.g., ERANGE for buffer too small)
 *
 * Note: getgrent_r is NOT part of POSIX. Use getgrent() for enumeration.
 */

/*
 * Get appropriate buffer size for group operations
 */
static size_t get_group_bufsize(void)
{
    long bufsize;

    bufsize = sysconf(_SC_GETGR_R_SIZE_MAX);
    if (bufsize == -1) {
        /* sysconf doesn't know, use reasonable default */
        bufsize = 4096;
    }

    return (size_t)bufsize;
}

/*
 * Print group information
 */
static void print_group(const struct group *grp)
{
    char **member;

    printf("  Group Name: %s\n", grp->gr_name);
    printf("  Group ID:   %d\n", (int)grp->gr_gid);
    printf("  Password:   %s\n", grp->gr_passwd ? grp->gr_passwd : "(none)");
    printf("  Members:    ");

    if (grp->gr_mem == NULL || grp->gr_mem[0] == NULL) {
        printf("(none)");
    } else {
        for (member = grp->gr_mem; *member != NULL; member++) {
            if (member != grp->gr_mem) {
                printf(", ");
            }
            printf("%s", *member);
        }
    }
    printf("\n");
}

/*
 * Example 1: Enumerate all groups using classic reentrant interface
 */
void enumerate_all_groups(void)
{
    struct group grp;           /* Caller-provided structure */
    char *buffer;               /* Caller-provided string buffer */
    size_t bufsize;
    struct group *result;
    int count = 0;

    printf("=== Enumerating All Groups ===\n\n");

    /*
     * Note: getgrent_r is NOT part of POSIX and has platform-specific signatures.
     * We use the simpler non-reentrant getgrent() for enumeration.
     * The returned data uses static storage, so process immediately.
     */

    /* Silence unused variable warnings */
    (void)grp;
    (void)buffer;
    (void)bufsize;
    (void)result;

    /*
     * Reset enumeration position to start
     * Note: This is process-wide, affects all threads
     */
    setgrent();

    /*
     * Non-reentrant getgrent():
     *   - No buffer management needed
     *   - Returns pointer to static data
     *   - Thread-unsafe, but simpler for single-threaded enumeration
     */
    {
        struct group *grp_ptr;
        while ((grp_ptr = getgrent()) != NULL) {
            count++;
            printf("Group #%d:\n", count);
            print_group(grp_ptr);
            printf("\n");
        }
    }

    /* Close database */
    endgrent();

    printf("Total groups enumerated: %d\n", count);
}

/*
 * Example 2: Lookup group by name using POSIX reentrant interface
 */
int lookup_group_by_name(const char *name)
{
    struct group grp;
    char *buffer;
    size_t bufsize;
    struct group *result;
    int ret;

    printf("=== Looking Up Group by Name: '%s' ===\n\n", name);

    bufsize = get_group_bufsize();
    buffer = malloc(bufsize);
    if (buffer == NULL) {
        perror("malloc");
        return -1;
    }

    /*
     * POSIX reentrant lookup by name:
     *   int getgrnam_r(name, &grp, buffer, bufsize, &result)
     *   Returns: 0 on success, error code on failure
     *   result: points to grp on success, NULL if not found
     */
    ret = getgrnam_r(name, &grp, buffer, bufsize, &result);

    if (ret == ERANGE) {
        fprintf(stderr, "Error: Buffer too small, retrying...\n");

        /* Retry with larger buffer */
        free(buffer);
        bufsize *= 2;
        buffer = malloc(bufsize);
        if (buffer != NULL) {
            ret = getgrnam_r(name, &grp, buffer, bufsize, &result);
        }
    }

    if (ret != 0) {
        fprintf(stderr, "getgrnam_r error: %s\n", strerror(ret));
    } else if (result == NULL) {
        printf("Group '%s' not found.\n", name);
    } else {
        printf("Found group:\n");
        print_group(&grp);
    }

    free(buffer);
    return (ret == 0 && result != NULL) ? 0 : -1;
}

/*
 * Example 3: Lookup group by GID using POSIX reentrant interface
 */
int lookup_group_by_gid(gid_t gid)
{
    struct group grp;
    char *buffer;
    size_t bufsize;
    struct group *result;
    int ret;

    printf("=== Looking Up Group by GID: %d ===\n\n", (int)gid);

    bufsize = get_group_bufsize();
    buffer = malloc(bufsize);
    if (buffer == NULL) {
        perror("malloc");
        return -1;
    }

    /*
     * POSIX reentrant lookup by GID:
     *   int getgrgid_r(gid, &grp, buffer, bufsize, &result)
     *   Returns: 0 on success, error code on failure
     *   result: points to grp on success, NULL if not found
     */
    ret = getgrgid_r(gid, &grp, buffer, bufsize, &result);

    if (ret == ERANGE) {
        fprintf(stderr, "Error: Buffer too small, retrying...\n");
        free(buffer);
        bufsize *= 2;
        buffer = malloc(bufsize);
        if (buffer != NULL) {
            ret = getgrgid_r(gid, &grp, buffer, bufsize, &result);
        }
    }

    if (ret != 0) {
        fprintf(stderr, "getgrgid_r error: %s\n", strerror(ret));
    } else if (result == NULL) {
        printf("Group with GID %d not found.\n", (int)gid);
    } else {
        printf("Found group:\n");
        print_group(&grp);
    }

    free(buffer);
    return (ret == 0 && result != NULL) ? 0 : -1;
}

/*
 * Example 4: Stack-allocated buffer (for small groups)
 *
 * This demonstrates using stack allocation when you know
 * groups are small. Not recommended for production code
 * where group membership could be large.
 */
void lookup_with_stack_buffer(const char *name)
{
    struct group grp;
    char buffer[1024];          /* Stack-allocated buffer */
    struct group *result;
    int ret;

    printf("=== Stack Buffer Lookup: '%s' ===\n\n", name);

    ret = getgrnam_r(name, &grp, buffer, sizeof(buffer), &result);

    if (ret == ERANGE) {
        printf("Group '%s' too large for stack buffer (%zu bytes)\n",
               name, sizeof(buffer));
        return;
    }

    if (ret != 0) {
        fprintf(stderr, "getgrnam_r error: %s\n", strerror(ret));
        return;
    }

    if (result == NULL) {
        printf("Group '%s' not found.\n", name);
        return;
    }

    printf("Found (using %zu byte stack buffer):\n", sizeof(buffer));
    print_group(&grp);
}

/*
 * Example 5: Demonstrate data lifetime
 *
 * With reentrant functions, data remains valid as long as
 * the struct and buffer remain in scope.
 */
void demonstrate_data_lifetime(void)
{
    struct group grp1, grp2;
    char *buffer1, *buffer2;
    size_t bufsize;
    struct group *result1, *result2;
    int ret1, ret2;

    printf("=== Data Lifetime Demonstration ===\n\n");

    bufsize = get_group_bufsize();
    buffer1 = malloc(bufsize);
    buffer2 = malloc(bufsize);

    if (buffer1 == NULL || buffer2 == NULL) {
        free(buffer1);
        free(buffer2);
        perror("malloc");
        return;
    }

    /* Look up two different groups */
    ret1 = getgrnam_r("root", &grp1, buffer1, bufsize, &result1);
    ret2 = getgrnam_r("sys", &grp2, buffer2, bufsize, &result2);

    printf("After both lookups, both results are still valid:\n\n");

    if (ret1 == 0 && result1 != NULL) {
        printf("First lookup (root):\n");
        print_group(&grp1);
        printf("\n");
    }

    if (ret2 == 0 && result2 != NULL) {
        printf("Second lookup (sys):\n");
        print_group(&grp2);
        printf("\n");
    }

    printf("Note: With non-reentrant getgrnam(), the second call\n");
    printf("would have overwritten the first result!\n");

    free(buffer1);
    free(buffer2);
}

/*
 * Memory layout visualization
 */
void show_memory_layout(void)
{
    struct group grp;
    char *buffer;
    size_t bufsize;
    struct group *result;
    int ret;

    printf("=== Memory Layout Visualization ===\n\n");

    bufsize = get_group_bufsize();
    buffer = malloc(bufsize);
    if (buffer == NULL) {
        perror("malloc");
        return;
    }

    ret = getgrnam_r("root", &grp, buffer, bufsize, &result);

    if (ret == 0 && result != NULL) {
        printf("struct group address:  %p\n", (void*)&grp);
        printf("buffer address:        %p\n", (void*)buffer);
        printf("buffer size:           %zu bytes\n\n", bufsize);

        printf("Pointer locations within struct group:\n");
        printf("  grp.gr_name:   %p", (void*)grp.gr_name);
        if (grp.gr_name >= buffer && grp.gr_name < buffer + bufsize) {
            printf(" (inside buffer, offset %ld)",
                   (long)(grp.gr_name - buffer));
        }
        printf("\n");

        printf("  grp.gr_passwd: %p", (void*)grp.gr_passwd);
        if (grp.gr_passwd >= buffer && grp.gr_passwd < buffer + bufsize) {
            printf(" (inside buffer, offset %ld)",
                   (long)(grp.gr_passwd - buffer));
        }
        printf("\n");

        printf("  grp.gr_mem:    %p", (void*)grp.gr_mem);
        if ((char*)grp.gr_mem >= buffer &&
            (char*)grp.gr_mem < buffer + bufsize) {
            printf(" (inside buffer, offset %ld)",
                   (long)((char*)grp.gr_mem - buffer));
        }
        printf("\n\n");

        printf("All string data and the member pointer array are\n");
        printf("stored within the caller-provided buffer.\n");
    }

    free(buffer);
}

int main(int argc, char *argv[])
{
    printf("AIX Classic Reentrant Group Database Example\n");
    printf("============================================\n\n");

    /* Show memory layout first */
    show_memory_layout();
    printf("\n");

    /* Lookup examples */
    lookup_group_by_name("root");
    printf("\n");

    lookup_group_by_name("sys");
    printf("\n");

    lookup_group_by_gid(0);
    printf("\n");

    /* Stack buffer example */
    lookup_with_stack_buffer("staff");
    printf("\n");

    /* Data lifetime demonstration */
    demonstrate_data_lifetime();
    printf("\n");

    /* Enumerate all groups (can be long on systems with many groups) */
    if (argc > 1 && strcmp(argv[1], "-a") == 0) {
        enumerate_all_groups();
    } else {
        printf("(Run with -a flag to enumerate all groups)\n");
    }

    return 0;
}
