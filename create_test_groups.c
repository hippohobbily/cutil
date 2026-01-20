/*
 * create_test_groups.c
 *
 * AIX C Program to Create Test Groups and Users Using Security APIs
 *
 * Uses putgroupattr() and putuserattr() to programmatically create
 * test configurations for getgrent testing.
 *
 * Compile on AIX:
 *   xlc -o create_test_groups create_test_groups.c -ls
 * or:
 *   gcc -o create_test_groups create_test_groups.c -ls
 *
 * Note: -ls links the security library (libs.a)
 *
 * Must be run as root.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <usersec.h>      /* AIX security subroutines */
#include <userconf.h>     /* User/group attribute definitions */

/* Configuration */
#define GROUP_PREFIX    "tgrp"
#define USER_PREFIX     "tusr"
#define BASE_GID        60000
#define BASE_UID        60000

/* Test sizes */
#define SMALL_SIZE      5
#define MEDIUM_SIZE     50
#define LARGE_SIZE      500
#define HUGE_SIZE       1900    /* Near AIX 2000 member limit */

/*----------------------------------------------------------------------
 * Utility Functions
 *----------------------------------------------------------------------*/

static void log_info(const char *msg)
{
    printf("[INFO] %s\n", msg);
}

static void log_error(const char *msg)
{
    fprintf(stderr, "[ERROR] %s: %s\n", msg, strerror(errno));
}

static int check_root(void)
{
    if (getuid() != 0) {
        fprintf(stderr, "This program must be run as root\n");
        return -1;
    }
    return 0;
}

/*----------------------------------------------------------------------
 * User Creation using putuserattr
 *----------------------------------------------------------------------*/

/*
 * Create a single test user using AIX security APIs
 *
 * putuserattr() signature:
 *   int putuserattr(char *user, char *attr, void *value, int type);
 *
 * To create a new user:
 *   1. Call putuserattr with SEC_NEW to create entry
 *   2. Set attributes (S_ID, S_PGRP, S_HOME, S_SHELL, etc.)
 *   3. Call putuserattr with SEC_COMMIT to persist
 */
static int create_user(const char *username, uid_t uid)
{
    int ret;
    char home[256];

    /* Check if user already exists */
    if (IDtouser(uid) != NULL) {
        printf("  User %s (UID=%d) already exists\n", username, uid);
        return 0;
    }

    log_info("Creating user...");
    printf("  Username: %s, UID: %d\n", username, uid);

    /* Open user database for writing */
    if (setuserdb(S_WRITE) != 0) {
        log_error("setuserdb(S_WRITE) failed");
        return -1;
    }

    /* Create new user entry */
    ret = putuserattr((char *)username, NULL, NULL, SEC_NEW);
    if (ret != 0) {
        log_error("putuserattr SEC_NEW failed");
        enduserdb();
        return -1;
    }

    /* Set user ID */
    ret = putuserattr((char *)username, S_ID, (void *)&uid, SEC_INT);
    if (ret != 0) {
        log_error("putuserattr S_ID failed");
        enduserdb();
        return -1;
    }

    /* Set primary group to "staff" */
    ret = putuserattr((char *)username, S_PGRP, (void *)"staff", SEC_CHAR);
    if (ret != 0) {
        log_error("putuserattr S_PGRP failed");
        enduserdb();
        return -1;
    }

    /* Set home directory */
    snprintf(home, sizeof(home), "/tmp/%s", username);
    ret = putuserattr((char *)username, S_HOME, (void *)home, SEC_CHAR);
    if (ret != 0) {
        log_error("putuserattr S_HOME failed");
        enduserdb();
        return -1;
    }

    /* Set shell */
    ret = putuserattr((char *)username, S_SHELL, (void *)"/usr/bin/ksh", SEC_CHAR);
    if (ret != 0) {
        log_error("putuserattr S_SHELL failed");
        enduserdb();
        return -1;
    }

    /* Commit changes */
    ret = putuserattr((char *)username, NULL, NULL, SEC_COMMIT);
    if (ret != 0) {
        log_error("putuserattr SEC_COMMIT failed");
        enduserdb();
        return -1;
    }

    /* Close database */
    enduserdb();

    return 0;
}

/*
 * Create multiple test users
 */
static int create_users(int count, int start_index)
{
    char username[32];
    uid_t uid;
    int i;
    int created = 0;

    printf("Creating %d test users starting from index %d...\n", count, start_index);

    for (i = start_index; i < start_index + count; i++) {
        snprintf(username, sizeof(username), "%s%04d", USER_PREFIX, i);
        uid = BASE_UID + i;

        if (create_user(username, uid) == 0) {
            created++;
        }

        /* Progress indicator */
        if (created % 100 == 0 && created > 0) {
            printf("  Created %d users...\n", created);
        }
    }

    printf("Created %d users\n", created);
    return created;
}

/*----------------------------------------------------------------------
 * Group Creation using putgroupattr
 *----------------------------------------------------------------------*/

/*
 * Create a single test group using AIX security APIs
 *
 * putgroupattr() signature:
 *   int putgroupattr(char *group, char *attr, void *value, int type);
 *
 * To create a new group:
 *   1. Call putgroupattr with SEC_NEW to create entry
 *   2. Set attributes (S_ID, S_USERS, etc.)
 *   3. Call putgroupattr with SEC_COMMIT to persist
 */
static int create_group(const char *groupname, gid_t gid)
{
    int ret;

    /* Check if group already exists */
    if (IDtogroup(gid) != NULL) {
        printf("  Group %s (GID=%d) already exists\n", groupname, gid);
        return 0;
    }

    log_info("Creating group...");
    printf("  Groupname: %s, GID: %d\n", groupname, gid);

    /* Open user database for writing (same DB for users and groups) */
    if (setuserdb(S_WRITE) != 0) {
        log_error("setuserdb(S_WRITE) failed");
        return -1;
    }

    /* Create new group entry */
    ret = putgroupattr((char *)groupname, NULL, NULL, SEC_NEW);
    if (ret != 0) {
        log_error("putgroupattr SEC_NEW failed");
        enduserdb();
        return -1;
    }

    /* Set group ID */
    ret = putgroupattr((char *)groupname, S_ID, (void *)&gid, SEC_INT);
    if (ret != 0) {
        log_error("putgroupattr S_ID failed");
        enduserdb();
        return -1;
    }

    /* Commit changes */
    ret = putgroupattr((char *)groupname, NULL, NULL, SEC_COMMIT);
    if (ret != 0) {
        log_error("putgroupattr SEC_COMMIT failed");
        enduserdb();
        return -1;
    }

    /* Close database */
    enduserdb();

    return 0;
}

/*
 * Build SEC_LIST format string for group members
 *
 * SEC_LIST format: "user1\0user2\0user3\0\0"
 * (strings separated by NUL, terminated by double NUL)
 *
 * Returns allocated buffer that caller must free
 */
static char *build_member_list(int count, int start_index)
{
    char *list;
    size_t total_size;
    char *ptr;
    char username[32];
    int i;

    /* Estimate size: count * (prefix + 4 digits + NUL) + final NUL */
    total_size = count * (strlen(USER_PREFIX) + 5 + 1) + 1;
    list = malloc(total_size);
    if (list == NULL) {
        return NULL;
    }

    ptr = list;
    for (i = start_index; i < start_index + count; i++) {
        snprintf(username, sizeof(username), "%s%04d", USER_PREFIX, i);
        strcpy(ptr, username);
        ptr += strlen(username) + 1;  /* Include the NUL */
    }
    *ptr = '\0';  /* Double NUL terminator */

    return list;
}

/*
 * Add members to a group
 *
 * S_USERS attribute expects SEC_LIST format
 */
static int add_members_to_group(const char *groupname, int member_count, int start_index)
{
    char *member_list;
    int ret;

    printf("Adding %d members to group %s...\n", member_count, groupname);

    /* Build member list in SEC_LIST format */
    member_list = build_member_list(member_count, start_index);
    if (member_list == NULL) {
        log_error("Failed to allocate member list");
        return -1;
    }

    /* Open database */
    if (setuserdb(S_WRITE) != 0) {
        log_error("setuserdb(S_WRITE) failed");
        free(member_list);
        return -1;
    }

    /* Set group members */
    ret = putgroupattr((char *)groupname, S_USERS, (void *)member_list, SEC_LIST);
    if (ret != 0) {
        log_error("putgroupattr S_USERS failed");
        enduserdb();
        free(member_list);
        return -1;
    }

    /* Commit changes */
    ret = putgroupattr((char *)groupname, NULL, NULL, SEC_COMMIT);
    if (ret != 0) {
        log_error("putgroupattr SEC_COMMIT failed");
        enduserdb();
        free(member_list);
        return -1;
    }

    enduserdb();
    free(member_list);

    printf("Added %d members to %s\n", member_count, groupname);
    return 0;
}

/*----------------------------------------------------------------------
 * Test Group Configurations
 *----------------------------------------------------------------------*/

/* Create empty group */
static int create_empty_group(void)
{
    char groupname[64];

    printf("\n=== Creating Empty Group ===\n");
    snprintf(groupname, sizeof(groupname), "%s_empty", GROUP_PREFIX);
    return create_group(groupname, BASE_GID + 1);
}

/* Create small group */
static int create_small_group(void)
{
    char groupname[64];

    printf("\n=== Creating Small Group (%d members) ===\n", SMALL_SIZE);

    /* Ensure users exist */
    create_users(SMALL_SIZE, 1);

    /* Create group */
    snprintf(groupname, sizeof(groupname), "%s_small", GROUP_PREFIX);
    if (create_group(groupname, BASE_GID + 2) != 0) {
        return -1;
    }

    /* Add members */
    return add_members_to_group(groupname, SMALL_SIZE, 1);
}

/* Create medium group */
static int create_medium_group(void)
{
    char groupname[64];

    printf("\n=== Creating Medium Group (%d members) ===\n", MEDIUM_SIZE);

    /* Ensure users exist */
    create_users(MEDIUM_SIZE, 1);

    /* Create group */
    snprintf(groupname, sizeof(groupname), "%s_medium", GROUP_PREFIX);
    if (create_group(groupname, BASE_GID + 3) != 0) {
        return -1;
    }

    /* Add members */
    return add_members_to_group(groupname, MEDIUM_SIZE, 1);
}

/* Create large group */
static int create_large_group(void)
{
    char groupname[64];

    printf("\n=== Creating Large Group (%d members) ===\n", LARGE_SIZE);

    /* Ensure users exist */
    create_users(LARGE_SIZE, 1);

    /* Create group */
    snprintf(groupname, sizeof(groupname), "%s_large", GROUP_PREFIX);
    if (create_group(groupname, BASE_GID + 4) != 0) {
        return -1;
    }

    /* Add members */
    return add_members_to_group(groupname, LARGE_SIZE, 1);
}

/* Create huge group (near 2000 member limit) */
static int create_huge_group(void)
{
    char groupname[64];

    printf("\n=== Creating Huge Group (%d members) ===\n", HUGE_SIZE);
    printf("This may take a while...\n");

    /* Ensure users exist */
    create_users(HUGE_SIZE, 1);

    /* Create group */
    snprintf(groupname, sizeof(groupname), "%s_huge", GROUP_PREFIX);
    if (create_group(groupname, BASE_GID + 5) != 0) {
        return -1;
    }

    /* Add members */
    return add_members_to_group(groupname, HUGE_SIZE, 1);
}

/*----------------------------------------------------------------------
 * Cleanup Functions
 *----------------------------------------------------------------------*/

static int remove_user(const char *username)
{
    int ret;

    if (setuserdb(S_WRITE) != 0) {
        return -1;
    }

    /* Delete user entry */
    ret = putuserattr((char *)username, NULL, NULL, SEC_DELETE);
    if (ret == 0) {
        ret = putuserattr((char *)username, NULL, NULL, SEC_COMMIT);
    }

    enduserdb();
    return ret;
}

static int remove_group(const char *groupname)
{
    int ret;

    if (setuserdb(S_WRITE) != 0) {
        return -1;
    }

    /* Delete group entry */
    ret = putgroupattr((char *)groupname, NULL, NULL, SEC_DELETE);
    if (ret == 0) {
        ret = putgroupattr((char *)groupname, NULL, NULL, SEC_COMMIT);
    }

    enduserdb();
    return ret;
}

static void cleanup_all(void)
{
    char name[64];
    int i;

    printf("\n=== Cleaning Up Test Groups ===\n");

    /* Remove test groups */
    const char *group_suffixes[] = {"_empty", "_small", "_medium", "_large", "_huge", NULL};
    for (i = 0; group_suffixes[i] != NULL; i++) {
        snprintf(name, sizeof(name), "%s%s", GROUP_PREFIX, group_suffixes[i]);
        printf("Removing group: %s\n", name);
        remove_group(name);
    }

    printf("\n=== Cleaning Up Test Users ===\n");

    /* Remove test users */
    for (i = 1; i <= HUGE_SIZE; i++) {
        snprintf(name, sizeof(name), "%s%04d", USER_PREFIX, i);
        if (i % 100 == 0) {
            printf("Removing users... (%d)\n", i);
        }
        remove_user(name);
    }

    printf("Cleanup complete\n");
}

/*----------------------------------------------------------------------
 * Status Display
 *----------------------------------------------------------------------*/

static void show_group_info(const char *groupname)
{
    gid_t gid;
    char *members;
    int count = 0;

    if (setuserdb(S_READ) != 0) {
        return;
    }

    if (getgroupattr((char *)groupname, S_ID, &gid, SEC_INT) == 0) {
        printf("  %s (GID=%d)\n", groupname, gid);

        if (getgroupattr((char *)groupname, S_USERS, &members, SEC_LIST) == 0) {
            /* Count members in SEC_LIST format */
            if (members != NULL && *members != '\0') {
                char *p = members;
                while (*p) {
                    count++;
                    p += strlen(p) + 1;
                }
            }
            printf("    Members: %d\n", count);
        }
    }

    enduserdb();
}

static void show_status(void)
{
    char name[64];
    int i;

    printf("\n=== Test Groups Status ===\n");

    const char *group_suffixes[] = {"_empty", "_small", "_medium", "_large", "_huge", NULL};
    for (i = 0; group_suffixes[i] != NULL; i++) {
        snprintf(name, sizeof(name), "%s%s", GROUP_PREFIX, group_suffixes[i]);
        show_group_info(name);
    }
}

/*----------------------------------------------------------------------
 * Main
 *----------------------------------------------------------------------*/

static void usage(const char *prog)
{
    printf("Usage: %s [command]\n", prog);
    printf("\nCommands:\n");
    printf("  setup   - Create basic test groups (empty, small, medium)\n");
    printf("  large   - Create large test group (%d members)\n", LARGE_SIZE);
    printf("  huge    - Create huge test group (%d members)\n", HUGE_SIZE);
    printf("  cleanup - Remove all test groups and users\n");
    printf("  status  - Show current test group configuration\n");
}

int main(int argc, char *argv[])
{
    const char *cmd;

    printf("AIX Test Group Creator (C Program using Security APIs)\n");
    printf("======================================================\n");

    if (check_root() != 0) {
        return 1;
    }

    cmd = (argc > 1) ? argv[1] : "setup";

    if (strcmp(cmd, "setup") == 0) {
        create_empty_group();
        create_small_group();
        create_medium_group();
        show_status();

    } else if (strcmp(cmd, "large") == 0) {
        create_large_group();
        show_status();

    } else if (strcmp(cmd, "huge") == 0) {
        create_huge_group();
        show_status();

    } else if (strcmp(cmd, "cleanup") == 0) {
        cleanup_all();

    } else if (strcmp(cmd, "status") == 0) {
        show_status();

    } else {
        usage(argv[0]);
        return 1;
    }

    return 0;
}
