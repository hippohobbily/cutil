/*
 * getgrent_test.c
 *
 * AIX getgrent/getgrent_r Test Program (No Root Required)
 *
 * Tests group database enumeration using AIX getgrent_r() or getgrent().
 * Features:
 *   - API tracing (strace-style) with return values and errno
 *   - Guarded buffers to detect buffer overflow/underflow (reentrant mode)
 *   - Configurable buffer size via -b option
 *   - Option to use non-reentrant getgrent() via -n option
 *
 * Buffer Layout (reentrant mode only):
 *   [HEAD_GUARD 64 bytes][USER_BUFFER N bytes][TAIL_GUARD 256 bytes]
 *   Guard regions filled with 0x5A, checked after each getgrent_r() call.
 *
 * AIX Reentrant Group Enumeration APIs:
 *   int setgrent_r(FILE **grpfp);
 *   int getgrent_r(struct group *grp, char *buffer, int buflen, FILE **grpfp);
 *   void endgrent_r(FILE **grpfp);
 *
 * AIX Non-Reentrant Group Enumeration APIs:
 *   void setgrent(void);
 *   struct group *getgrent(void);
 *   void endgrent(void);
 *
 * Compile on AIX:
 *   xlc_r -o getgrent_test getgrent_test.c
 *   gcc -D_THREAD_SAFE -o getgrent_test getgrent_test.c
 *
 * Usage:
 *   ./getgrent_test              # Enumerate with default buffer (4096)
 *   ./getgrent_test -b 2048      # Enumerate with 2048 byte buffer
 *   ./getgrent_test -n           # Use non-reentrant getgrent()
 *   ./getgrent_test -a           # Show all groups (not just test groups)
 *   ./getgrent_test -h           # Show help
 */

#define _THREAD_SAFE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <grp.h>

#define DEFAULT_BUFLEN 4096

/* Guard region settings for overflow detection */
#define GUARD_FILL      0x5A
#define HEAD_GUARD_SIZE 64
#define TAIL_GUARD_SIZE 256

typedef struct {
    size_t          user_size;      /* Size requested by user */
    size_t          total_size;     /* Total allocated size */
    unsigned char   *raw;           /* Raw allocation */
    unsigned char   *head_guard;    /* Head guard region */
    unsigned char   *buffer;        /* User buffer (passed to getgrent_r) */
    unsigned char   *tail_guard;    /* Tail guard region */
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

    g->head_guard = g->raw;
    g->buffer = g->raw + HEAD_GUARD_SIZE;
    g->tail_guard = g->buffer + size;

    /* Fill guard regions with known pattern */
    memset(g->head_guard, GUARD_FILL, HEAD_GUARD_SIZE);
    memset(g->buffer, 0, size);  /* Zero the user buffer */
    memset(g->tail_guard, GUARD_FILL, TAIL_GUARD_SIZE);

    return g;
}

static void guarded_free(guarded_buf_t *g)
{
    if (g) {
        free(g->raw);
        free(g);
    }
}

/* Returns number of guard violations (0 = no overflow) */
static int guarded_check(guarded_buf_t *g, const char *context)
{
    int errors = 0;
    size_t i;

    /* Check head guard (underflow detection) */
    for (i = 0; i < HEAD_GUARD_SIZE; i++) {
        if (g->head_guard[i] != GUARD_FILL) {
            if (errors < 3)
                fprintf(stderr, "[UNDERFLOW] %s: head_guard[%zu]=0x%02X (expected 0x%02X)\n",
                        context, i, g->head_guard[i], GUARD_FILL);
            errors++;
        }
    }

    /* Check tail guard (overflow detection) */
    for (i = 0; i < TAIL_GUARD_SIZE; i++) {
        if (g->tail_guard[i] != GUARD_FILL) {
            if (errors < 3)
                fprintf(stderr, "[OVERFLOW] %s: tail_guard[%zu]=0x%02X (expected 0x%02X)\n",
                        context, i, g->tail_guard[i], GUARD_FILL);
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
 * Uses static internal buffer - not thread-safe but simpler.
 */
static void enumerate_groups_nonreentrant(int show_all)
{
    struct group *grp;
    int count = 0;
    int test_found = 0;
    int saved_errno;

    printf("=== Enumerating Groups (non-reentrant getgrent) ===\n\n");
    printf("Note: getgrent() uses static storage, no user buffer needed.\n");
    printf("      Data may be overwritten by subsequent calls.\n\n");

    /* setgrent() */
    printf("[CALL] setgrent()\n");
    errno = 0;
    setgrent();
    saved_errno = errno;
    printf("[RESULT] errno=%d", saved_errno);
    if (saved_errno != 0) {
        printf(" (%s)", strerror(saved_errno));
    }
    printf("\n\n");

    /* getgrent() loop */
    printf("[CALL] getgrent() in loop...\n\n");

    while (1) {
        errno = 0;
        grp = getgrent();
        saved_errno = errno;

        if (grp == NULL) {
            printf("[CALL] getgrent()\n");
            printf("[RESULT] return=NULL, errno=%d", saved_errno);
            if (saved_errno != 0) {
                printf(" (%s)", strerror(saved_errno));
            }
            printf("\n");
            break;
        }

        count++;

        /* Check if test group */
        int is_test = (strncmp(grp->gr_name, "ztest_", 6) == 0 ||
                       strncmp(grp->gr_name, "ZTEST", 5) == 0);

        if (is_test) {
            test_found = 1;
        }

        if (show_all || is_test) {
            printf("[CALL] getgrent()\n");
            printf("[RESULT] return=%p, errno=%d\n", (void *)grp, saved_errno);
            printf("Group #%d:\n", count);
            print_group(grp);
            printf("\n");
        }
    }

    /* endgrent() */
    printf("\n[CALL] endgrent()\n");
    errno = 0;
    endgrent();
    saved_errno = errno;
    printf("[RESULT] errno=%d", saved_errno);
    if (saved_errno != 0) {
        printf(" (%s)", strerror(saved_errno));
    }
    printf("\n");

    /* Summary */
    printf("\n=== Summary ===\n");
    printf("API mode: non-reentrant (getgrent)\n");
    printf("Total groups enumerated: %d\n", count);

    if (!show_all) {
        if (test_found) {
            printf("Test groups (ztest_*/ZTEST*) found.\n");
        } else {
            printf("No test groups found (ztest_*/ZTEST*).\n");
            printf("Run: setup_test_groups.sh setup\n");
        }
    }
}

/*
 * Enumerate all groups using AIX getgrent_r()
 */
static void enumerate_groups_reentrant(int buflen, int show_all)
{
    struct group grp;
    guarded_buf_t *gbuf;
    FILE *grpfp = NULL;
    int ret;
    int count = 0;
    int test_found = 0;
    int saved_errno;
    int overflow_detected = 0;

    printf("=== Enumerating Groups (AIX getgrent_r) ===\n\n");
    printf("Buffer size: %d bytes\n", buflen);
    printf("Guard regions: head=%d bytes, tail=%d bytes\n", HEAD_GUARD_SIZE, TAIL_GUARD_SIZE);
    printf("Total allocated: %d bytes\n\n", buflen + HEAD_GUARD_SIZE + TAIL_GUARD_SIZE);

    /* Allocate guarded buffer */
    gbuf = guarded_alloc(buflen);
    if (gbuf == NULL) {
        perror("malloc");
        return;
    }

    /* setgrent_r() */
    printf("[CALL] setgrent_r(&grpfp) where grpfp=%p\n", (void *)grpfp);
    errno = 0;
    ret = setgrent_r(&grpfp);
    saved_errno = errno;
    printf("[RESULT] return=%d, grpfp=%p, errno=%d", ret, (void *)grpfp, saved_errno);
    if (saved_errno != 0) {
        printf(" (%s)", strerror(saved_errno));
    }
    printf("\n\n");

    if (ret != 0) {
        printf("[ERROR] setgrent_r failed\n");
        guarded_free(gbuf);
        return;
    }

    /* getgrent_r() loop */
    printf("[CALL] getgrent_r(&grp, buffer, %d, &grpfp) in loop...\n\n", buflen);

    while (1) {
        errno = 0;
        ret = getgrent_r(&grp, (char *)gbuf->buffer, buflen, &grpfp);
        saved_errno = errno;

        /* Check for buffer overflow after each call */
        if (guarded_check(gbuf, grp.gr_name ? grp.gr_name : "(null)") != 0) {
            overflow_detected = 1;
            printf("[CRITICAL] Buffer overflow detected!\n");
        }

        if (ret != 0) {
            printf("[CALL] getgrent_r(&grp, buffer, %d, &grpfp)\n", buflen);
            printf("[RESULT] return=%d, errno=%d", ret, saved_errno);
            if (saved_errno != 0) {
                printf(" (%s)", strerror(saved_errno));
            }
            if (saved_errno == ERANGE) {
                printf(" - buffer too small!");
            }
            printf("\n");
            break;
        }

        count++;

        /* Check if test group (lowercase for AIX, uppercase for IBM i PASE) */
        int is_test = (strncmp(grp.gr_name, "ztest_", 6) == 0 ||
                       strncmp(grp.gr_name, "ZTEST", 5) == 0);

        if (is_test) {
            test_found = 1;
        }

        if (show_all || is_test) {
            printf("[CALL] getgrent_r(&grp, buffer, %d, &grpfp)\n", buflen);
            printf("[RESULT] return=%d, errno=%d\n", ret, saved_errno);
            printf("Group #%d:\n", count);
            print_group(&grp);
            printf("\n");
        }
    }

    /* endgrent_r() */
    printf("\n[CALL] endgrent_r(&grpfp) where grpfp=%p\n", (void *)grpfp);
    errno = 0;
    endgrent_r(&grpfp);
    saved_errno = errno;
    printf("[RESULT] grpfp=%p, errno=%d", (void *)grpfp, saved_errno);
    if (saved_errno != 0) {
        printf(" (%s)", strerror(saved_errno));
    }
    printf("\n");

    /* Summary */
    printf("\n=== Summary ===\n");
    printf("Buffer size: %d bytes\n", buflen);
    printf("Guard regions: head=%d, tail=%d bytes\n", HEAD_GUARD_SIZE, TAIL_GUARD_SIZE);
    printf("Total groups enumerated: %d\n", count);

    if (overflow_detected) {
        printf("\n[CRITICAL] BUFFER OVERFLOW WAS DETECTED!\n");
        printf("The getgrent_r() function wrote beyond the %d byte buffer.\n", buflen);
    } else {
        printf("\n[OK] No buffer overflow detected - guard regions intact.\n");
    }

    if (!show_all) {
        if (test_found) {
            printf("Test groups (ztest_*/ZTEST*) found.\n");
        } else {
            printf("No test groups found (ztest_*/ZTEST*).\n");
            printf("Run: setup_test_groups.sh setup\n");
        }
    }

    guarded_free(gbuf);
}

static void usage(const char *prog)
{
    printf("Usage: %s [options]\n\n", prog);
    printf("Enumerate groups using AIX getgrent_r() or getgrent() with API tracing.\n");
    printf("Uses guarded buffers to detect buffer overflow (reentrant mode).\n\n");
    printf("Options:\n");
    printf("  -b <size>    Buffer size in bytes (default: %d)\n", DEFAULT_BUFLEN);
    printf("  -n           Use non-reentrant getgrent() instead of getgrent_r()\n");
    printf("  -a           Show all groups (default: only test groups)\n");
    printf("  -h           Show this help\n");
    printf("\nExamples:\n");
    printf("  %s                 Enumerate with %d byte buffer (reentrant)\n", prog, DEFAULT_BUFLEN);
    printf("  %s -b 2048         Enumerate with 2048 byte buffer\n", prog);
    printf("  %s -b 256          Smaller buffer (test overflow detection)\n", prog);
    printf("  %s -n              Use non-reentrant getgrent()\n", prog);
    printf("  %s -n -a           Non-reentrant, show all groups\n", prog);
    printf("  %s -a              Show all groups\n", prog);
    printf("\nGuard regions (reentrant mode only):\n");
    printf("  Head guard: %d bytes before buffer (detect underflow)\n", HEAD_GUARD_SIZE);
    printf("  Tail guard: %d bytes after buffer (detect overflow)\n", TAIL_GUARD_SIZE);
    printf("\nAIX Reentrant APIs (-b mode, default):\n");
    printf("  int setgrent_r(FILE **grpfp)\n");
    printf("  int getgrent_r(struct group *grp, char *buf, int buflen, FILE **grpfp)\n");
    printf("  void endgrent_r(FILE **grpfp)\n");
    printf("\nAIX Non-Reentrant APIs (-n mode):\n");
    printf("  void setgrent(void)\n");
    printf("  struct group *getgrent(void)\n");
    printf("  void endgrent(void)\n");
    printf("\nSetup test groups first (requires root):\n");
    printf("  ./setup_test_groups.sh setup 50\n");
}

int main(int argc, char *argv[])
{
    int opt;
    int buflen = DEFAULT_BUFLEN;
    int show_all = 0;
    int use_nonreentrant = 0;

    printf("AIX getgrent Test\n");
    printf("=================\n\n");

    while ((opt = getopt(argc, argv, "b:nah")) != -1) {
        switch (opt) {
        case 'b':
            buflen = atoi(optarg);
            if (buflen < 32) {
                fprintf(stderr, "Buffer size too small, using 32\n");
                buflen = 32;
            }
            break;
        case 'n':
            use_nonreentrant = 1;
            break;
        case 'a':
            show_all = 1;
            break;
        case 'h':
        default:
            usage(argv[0]);
            return (opt == 'h') ? 0 : 1;
        }
    }

    if (use_nonreentrant) {
        enumerate_groups_nonreentrant(show_all);
    } else {
        enumerate_groups_reentrant(buflen, show_all);
    }

    return 0;
}
