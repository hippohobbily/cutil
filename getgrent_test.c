/*
 * getgrent_test.c
 *
 * AIX getgrent_r Test Program (No Root Required)
 *
 * Tests group database enumeration using AIX reentrant getgrent_r().
 * Displays API tracing with return values, errno, and buffer sizes.
 *
 * AIX Reentrant Group Enumeration APIs:
 *   int setgrent_r(FILE **grpfp);
 *   int getgrent_r(struct group *grp, char *buffer, int buflen, FILE **grpfp);
 *   void endgrent_r(FILE **grpfp);
 *
 * Compile on AIX:
 *   xlc_r -o getgrent_test getgrent_test.c
 *   gcc -D_THREAD_SAFE -o getgrent_test getgrent_test.c
 *
 * Usage:
 *   ./getgrent_test              # Enumerate with default buffer (4096)
 *   ./getgrent_test -b 256       # Enumerate with 256 byte buffer
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
 * Enumerate all groups using AIX getgrent_r()
 */
static void enumerate_groups(int buflen, int show_all)
{
    struct group grp;
    char *buffer;
    FILE *grpfp = NULL;
    int ret;
    int count = 0;
    int test_found = 0;
    int saved_errno;

    printf("=== Enumerating Groups (AIX getgrent_r) ===\n\n");
    printf("Buffer size: %d bytes\n\n", buflen);

    /* Allocate buffer */
    buffer = malloc(buflen);
    if (buffer == NULL) {
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
        free(buffer);
        return;
    }

    /* getgrent_r() loop */
    printf("[CALL] getgrent_r(&grp, buffer, %d, &grpfp) in loop...\n\n", buflen);

    while (1) {
        errno = 0;
        ret = getgrent_r(&grp, buffer, buflen, &grpfp);
        saved_errno = errno;

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
    printf("Total groups enumerated: %d\n", count);

    if (!show_all) {
        if (test_found) {
            printf("Test groups (ztest_*/ZTEST*) found.\n");
        } else {
            printf("No test groups found (ztest_*/ZTEST*).\n");
            printf("Run: setup_test_groups.sh setup\n");
        }
    }

    free(buffer);
}

static void usage(const char *prog)
{
    printf("Usage: %s [options]\n\n", prog);
    printf("Enumerate groups using AIX getgrent_r() with API tracing.\n\n");
    printf("Options:\n");
    printf("  -b <size>    Buffer size in bytes (default: %d)\n", DEFAULT_BUFLEN);
    printf("  -a           Show all groups (default: only test groups)\n");
    printf("  -h           Show this help\n");
    printf("\nExamples:\n");
    printf("  %s                 Enumerate with %d byte buffer\n", prog, DEFAULT_BUFLEN);
    printf("  %s -b 256          Enumerate with 256 byte buffer\n", prog);
    printf("  %s -b 64           Small buffer (may trigger ERANGE)\n", prog);
    printf("  %s -a              Show all groups\n", prog);
    printf("\nAIX APIs used:\n");
    printf("  int setgrent_r(FILE **grpfp)\n");
    printf("  int getgrent_r(struct group *grp, char *buf, int buflen, FILE **grpfp)\n");
    printf("  void endgrent_r(FILE **grpfp)\n");
    printf("\nSetup test groups first (requires root):\n");
    printf("  ./setup_test_groups.sh setup 50\n");
}

int main(int argc, char *argv[])
{
    int opt;
    int buflen = DEFAULT_BUFLEN;
    int show_all = 0;

    printf("AIX getgrent_r Test\n");
    printf("===================\n\n");

    while ((opt = getopt(argc, argv, "b:ah")) != -1) {
        switch (opt) {
        case 'b':
            buflen = atoi(optarg);
            if (buflen < 32) {
                fprintf(stderr, "Buffer size too small, using 32\n");
                buflen = 32;
            }
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

    enumerate_groups(buflen, show_all);

    return 0;
}
