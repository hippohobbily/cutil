/*
 * getgrent_test.c
 *
 * AIX getgrent Test Program (No Root Required)
 *
 * Tests group database access with guarded buffers to detect overflow.
 * Run setup_test_groups.sh as root first to create test groups.
 *
 * Compile on AIX:
 *   xlc_r -o getgrent_test getgrent_test.c
 *   gcc -D_THREAD_SAFE -o getgrent_test getgrent_test.c
 *
 * Usage:
 *   ./getgrent_test                    # Enumerate all groups
 *   ./getgrent_test -g <groupname>     # Lookup specific group
 *   ./getgrent_test -b <bufsize>       # Use specific buffer size
 *   ./getgrent_test -g ztest_grp -b 64 # Lookup with small buffer
 */

#define _THREAD_SAFE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <grp.h>

/*
 * AIX POSIX Reentrant APIs:
 *   int getgrnam_r(name, grp, buf, buflen, &result)
 *   int getgrgid_r(gid, grp, buf, buflen, &result)
 */

#define GUARD_FILL      0x5A
#define BUFFER_FILL     0xAA
#define HEAD_GUARD_SIZE 64
#define TAIL_GUARD_SIZE 256

typedef struct {
    size_t          user_size;
    size_t          total_size;
    unsigned char   *raw;
    unsigned char   *head;
    unsigned char   *buffer;
    unsigned char   *tail;
} guarded_buf_t;

static guarded_buf_t *guarded_alloc(size_t size)
{
    guarded_buf_t *g = malloc(sizeof(guarded_buf_t));
    if (!g) return NULL;

    g->user_size = size;
    g->total_size = HEAD_GUARD_SIZE + size + TAIL_GUARD_SIZE;
    g->raw = malloc(g->total_size);
    if (!g->raw) {
        free(g);
        return NULL;
    }

    g->head = g->raw;
    g->buffer = g->raw + HEAD_GUARD_SIZE;
    g->tail = g->buffer + size;

    memset(g->head, GUARD_FILL, HEAD_GUARD_SIZE);
    memset(g->buffer, BUFFER_FILL, size);
    memset(g->tail, GUARD_FILL, TAIL_GUARD_SIZE);

    return g;
}

static void guarded_free(guarded_buf_t *g)
{
    if (g) {
        free(g->raw);
        free(g);
    }
}

static int guarded_check(guarded_buf_t *g, const char *context)
{
    int errors = 0;
    size_t i;

    for (i = 0; i < HEAD_GUARD_SIZE; i++) {
        if (g->head[i] != GUARD_FILL) {
            if (errors < 3)
                fprintf(stderr, "[UNDERFLOW] %s: head[%zu]=0x%02X\n",
                        context, i, g->head[i]);
            errors++;
        }
    }

    for (i = 0; i < TAIL_GUARD_SIZE; i++) {
        if (g->tail[i] != GUARD_FILL) {
            if (errors < 3)
                fprintf(stderr, "[OVERFLOW] %s: tail[%zu]=0x%02X\n",
                        context, i, g->tail[i]);
            errors++;
        }
    }

    if (errors > 3)
        fprintf(stderr, "  ... %d more guard violations\n", errors - 3);

    return errors;
}

static void print_group(const struct group *grp)
{
    int count = 0;
    char **m;

    printf("  Name:     %s\n", grp->gr_name);
    printf("  GID:      %d\n", (int)grp->gr_gid);
    printf("  Password: %s\n", grp->gr_passwd ? grp->gr_passwd : "(none)");

    if (grp->gr_mem) {
        for (m = grp->gr_mem; *m; m++) count++;
    }
    printf("  Members:  %d\n", count);

    if (count > 0 && count <= 10) {
        printf("  List:     ");
        for (m = grp->gr_mem; *m; m++) {
            printf("%s%s", (m == grp->gr_mem) ? "" : ", ", *m);
        }
        printf("\n");
    }
}

/*
 * Enumerate all groups using non-reentrant getgrent()
 */
static void enumerate_groups(void)
{
    struct group *grp;
    int count = 0;
    int test_found = 0;

    printf("=== Enumerating All Groups ===\n\n");

    setgrent();
    while ((grp = getgrent()) != NULL) {
        count++;

        /* Show test groups */
        if (strncmp(grp->gr_name, "ztest_", 6) == 0) {
            printf("Group #%d:\n", count);
            print_group(grp);
            printf("\n");
            test_found = 1;
        }
    }
    endgrent();

    printf("Total groups: %d\n", count);
    if (!test_found) {
        printf("\nNo test groups found (ztest_*).\n");
        printf("Run: setup_test_groups.sh setup\n");
    }
}

/*
 * Lookup group by name with guarded buffer
 */
static void lookup_group(const char *name, size_t bufsize)
{
    struct group grp;
    struct group *result;
    guarded_buf_t *g;
    int ret;

    printf("=== Lookup Group: %s ===\n", name);
    printf("Buffer size: %zu bytes\n", bufsize);
    printf("Guard regions: head=%d, tail=%d bytes\n\n",
           HEAD_GUARD_SIZE, TAIL_GUARD_SIZE);

    g = guarded_alloc(bufsize);
    if (!g) {
        perror("malloc");
        return;
    }

    ret = getgrnam_r(name, &grp, (char *)g->buffer, bufsize, &result);

    printf("Return: %d", ret);
    if (ret == ERANGE) printf(" (ERANGE - buffer too small)");
    else if (ret != 0) printf(" (%s)", strerror(ret));
    printf("\n");

    printf("Result: %s\n\n", result ? "found" : "NULL");

    /* Check guard regions */
    if (guarded_check(g, name) == 0) {
        printf("[OK] Guard regions intact\n\n");
    } else {
        printf("[CRITICAL] Buffer overflow detected!\n\n");
    }

    if (ret == 0 && result) {
        print_group(&grp);
    } else if (ret == ERANGE) {
        printf("Try larger buffer: -b %zu\n", bufsize * 2);
    } else if (ret == 0 && !result) {
        printf("Group '%s' not found.\n", name);
    }

    guarded_free(g);
}

/*
 * Progressive buffer sizing test
 */
static void test_progressive(const char *name)
{
    struct group grp;
    struct group *result;
    guarded_buf_t *g = NULL;
    int ret;
    size_t sizes[] = {32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 0};
    int i;

    printf("=== Progressive Buffer Test: %s ===\n\n", name);

    for (i = 0; sizes[i] != 0; i++) {
        if (g) guarded_free(g);
        g = guarded_alloc(sizes[i]);
        if (!g) {
            perror("malloc");
            return;
        }

        ret = getgrnam_r(name, &grp, (char *)g->buffer, sizes[i], &result);

        printf("  %5zu bytes: ", sizes[i]);

        if (ret == ERANGE) {
            printf("ERANGE");
            if (guarded_check(g, "progressive") != 0)
                printf(" [OVERFLOW!]");
            printf("\n");
            continue;
        }

        if (ret != 0) {
            printf("error %d\n", ret);
            break;
        }

        if (!result) {
            printf("not found\n");
            break;
        }

        /* Success */
        int members = 0;
        if (grp.gr_mem) {
            char **m;
            for (m = grp.gr_mem; *m; m++) members++;
        }
        printf("OK - %d members", members);
        if (guarded_check(g, "progressive") != 0)
            printf(" [OVERFLOW!]");
        printf("\n");
        break;
    }

    if (sizes[i] == 0)
        printf("\nFailed: buffer too small even at 8192 bytes\n");

    if (g) guarded_free(g);
}

static void usage(const char *prog)
{
    printf("Usage: %s [options]\n\n", prog);
    printf("Options:\n");
    printf("  -g <name>    Lookup specific group\n");
    printf("  -b <size>    Buffer size (default: 4096)\n");
    printf("  -p <name>    Progressive buffer test for group\n");
    printf("  -e           Enumerate all groups\n");
    printf("  -h           Show this help\n");
    printf("\nExamples:\n");
    printf("  %s -e                    Enumerate all groups\n", prog);
    printf("  %s -g root               Lookup root group\n", prog);
    printf("  %s -g ztest_grp -b 64    Test with small buffer\n", prog);
    printf("  %s -p ztest_grp          Progressive buffer test\n", prog);
    printf("\nSetup test groups first:\n");
    printf("  # as root\n");
    printf("  ./setup_test_groups.sh setup 50\n");
}

int main(int argc, char *argv[])
{
    int opt;
    char *groupname = NULL;
    size_t bufsize = 4096;
    int do_enumerate = 0;
    int do_progressive = 0;

    printf("AIX getgrent Test\n");
    printf("=================\n\n");

    if (argc == 1) {
        do_enumerate = 1;
    }

    while ((opt = getopt(argc, argv, "g:b:p:eh")) != -1) {
        switch (opt) {
        case 'g':
            groupname = optarg;
            break;
        case 'b':
            bufsize = (size_t)atoi(optarg);
            if (bufsize < 32) bufsize = 32;
            break;
        case 'p':
            groupname = optarg;
            do_progressive = 1;
            break;
        case 'e':
            do_enumerate = 1;
            break;
        case 'h':
        default:
            usage(argv[0]);
            return (opt == 'h') ? 0 : 1;
        }
    }

    if (do_enumerate) {
        enumerate_groups();
    } else if (do_progressive && groupname) {
        test_progressive(groupname);
    } else if (groupname) {
        lookup_group(groupname, bufsize);
    } else {
        usage(argv[0]);
        return 1;
    }

    return 0;
}
