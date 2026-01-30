/*
 * listptf.c - List PTFs installed on IBM i
 *
 * Uses systemCL() to run SQL queries against QSYS2.PTF_INFO
 * and outputs PTF list with applied/effective status.
 *
 * Compile on IBM i (from PASE):
 *   gcc -o listptf listptf.c -maix64
 *
 * Or from ILE:
 *   CRTBNDC PGM(MYLIB/LISTPTF) SRCSTMF('/path/to/listptf.c')
 *
 * Usage:
 *   ./listptf                     # List all PTFs
 *   ./listptf 5770SS1             # List PTFs for product
 *   ./listptf -o /tmp/out.txt     # Specify output file
 *   ./listptf -s                  # Summary only
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

/*
 * PASE header for systemCL()
 * systemCL() runs a CL command from PASE
 */
#ifdef __PASE__
#include <as400_protos.h>
#endif

/*-------------------------------------------------------------------*/
/* Constants                                                          */
/*-------------------------------------------------------------------*/

#define MAX_PRODUCTS    10
#define MAX_SQL_LEN     4096
#define MAX_PATH_LEN    256
#define MAX_LINE_LEN    512

/*-------------------------------------------------------------------*/
/* Global options                                                     */
/*-------------------------------------------------------------------*/

static char g_output_file[MAX_PATH_LEN] = "";
static char g_products[MAX_PRODUCTS][10];
static int  g_product_count = 0;
static int  g_verbose = 0;
static int  g_summary = 0;

/*-------------------------------------------------------------------*/
/* Function prototypes                                                */
/*-------------------------------------------------------------------*/

static void usage(const char *prog);
static int  run_cl_command(const char *cmd);
static int  run_sql_to_file(const char *sql, const char *outfile);
static void build_sql_query(char *sql, size_t sql_len);
static void build_sql_summary(char *sql, size_t sql_len);
static const char *get_home_dir(void);
static void print_file_head(const char *path, int lines);

/*-------------------------------------------------------------------*/
/* Main                                                               */
/*-------------------------------------------------------------------*/

int main(int argc, char *argv[])
{
    int opt;
    char sql[MAX_SQL_LEN];
    char temp_sql_file[MAX_PATH_LEN];
    char temp_out_file[MAX_PATH_LEN];
    char cl_cmd[MAX_SQL_LEN + 256];
    time_t now;
    struct tm *tm_info;
    char timestamp[64];
    FILE *fp;
    int rc;

    /* Parse command line options */
    while ((opt = getopt(argc, argv, "o:svh")) != -1) {
        switch (opt) {
        case 'o':
            strncpy(g_output_file, optarg, sizeof(g_output_file) - 1);
            break;
        case 's':
            g_summary = 1;
            break;
        case 'v':
            g_verbose = 1;
            break;
        case 'h':
        default:
            usage(argv[0]);
            return (opt == 'h') ? 0 : 1;
        }
    }

    /* Remaining arguments are product IDs */
    for (int i = optind; i < argc && g_product_count < MAX_PRODUCTS; i++) {
        strncpy(g_products[g_product_count], argv[i], 9);
        g_products[g_product_count][9] = '\0';
        g_product_count++;
    }

    /* Set default output file if not specified */
    if (g_output_file[0] == '\0') {
        const char *home = get_home_dir();
        snprintf(g_output_file, sizeof(g_output_file),
                 "%s/ptf_list.txt", home);
    }

    /* Get timestamp */
    time(&now);
    tm_info = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

    /* Print header to console */
    printf("==============================================\n");
    printf("IBM i PTF List\n");
    printf("==============================================\n\n");
    printf("Date:       %s\n", timestamp);
    printf("Output:     %s\n", g_output_file);
    if (g_product_count > 0) {
        printf("Products:   ");
        for (int i = 0; i < g_product_count; i++) {
            printf("%s ", g_products[i]);
        }
        printf("\n");
    } else {
        printf("Products:   ALL\n");
    }
    printf("\n");

    /* Build SQL query */
    if (g_summary) {
        build_sql_summary(sql, sizeof(sql));
    } else {
        build_sql_query(sql, sizeof(sql));
    }

    /* Create temporary SQL file */
    snprintf(temp_sql_file, sizeof(temp_sql_file), "/tmp/listptf_%d.sql", getpid());
    snprintf(temp_out_file, sizeof(temp_out_file), "/tmp/listptf_%d.out", getpid());

    fp = fopen(temp_sql_file, "w");
    if (fp == NULL) {
        fprintf(stderr, "[ERROR] Cannot create temp file: %s\n", strerror(errno));
        return 1;
    }
    fprintf(fp, "%s\n", sql);
    fclose(fp);

    printf("[INFO] Querying PTF information...\n");

    /*
     * Method 1: Use RUNSQLSTM to run SQL from file
     * This works because RUNSQLSTM can run SELECT with OUTPUT
     */
    snprintf(cl_cmd, sizeof(cl_cmd),
             "RUNSQLSTM SRCSTMF('%s') COMMIT(*NONE) OUTPUT(*PRINT)",
             temp_sql_file);

#ifdef __PASE__
    /*
     * systemCL() runs a CL command from PASE
     * Returns 0 on success, -1 on error
     */
    rc = systemCL(cl_cmd, 0);
#else
    /*
     * Fallback for non-PASE compilation (testing on other systems)
     * Use system() with QSH
     */
    char qsh_cmd[MAX_SQL_LEN + 512];
    snprintf(qsh_cmd, sizeof(qsh_cmd),
             "system \"%s\" > '%s' 2>&1",
             cl_cmd, temp_out_file);
    rc = system(qsh_cmd);
#endif

    if (rc != 0) {
        fprintf(stderr, "[WARN] RUNSQLSTM returned %d, trying db2 utility...\n", rc);

        /*
         * Method 2: Use QShell db2 utility
         * This is more reliable for SELECT queries
         */
        char db2_cmd[MAX_SQL_LEN + 256];
        snprintf(db2_cmd, sizeof(db2_cmd),
                 "/QOpenSys/usr/bin/qsh -c \"db2 -t -f '%s'\" > '%s' 2>&1",
                 temp_sql_file, temp_out_file);

#ifdef __PASE__
        rc = system(db2_cmd);
#else
        rc = system(db2_cmd);
#endif
    }

    /* Write header to output file */
    fp = fopen(g_output_file, "w");
    if (fp == NULL) {
        fprintf(stderr, "[ERROR] Cannot create output file: %s\n", strerror(errno));
        unlink(temp_sql_file);
        return 1;
    }

    fprintf(fp, "==============================================\n");
    fprintf(fp, "IBM i PTF List\n");
    fprintf(fp, "==============================================\n\n");
    fprintf(fp, "Generated: %s\n", timestamp);
    if (g_product_count > 0) {
        fprintf(fp, "Products:  ");
        for (int i = 0; i < g_product_count; i++) {
            fprintf(fp, "%s ", g_products[i]);
        }
        fprintf(fp, "\n");
    } else {
        fprintf(fp, "Products:  ALL\n");
    }
    fprintf(fp, "\n----------------------------------------------\n\n");

    if (g_summary) {
        fprintf(fp, "=== PTF Summary by Product and Status ===\n\n");
    } else if (g_verbose) {
        fprintf(fp, "=== PTF Details (Verbose) ===\n\n");
    } else {
        fprintf(fp, "=== PTF List ===\n\n");
    }

    /* Append query output if file exists */
    FILE *tmp_fp = fopen(temp_out_file, "r");
    if (tmp_fp != NULL) {
        char line[MAX_LINE_LEN];
        while (fgets(line, sizeof(line), tmp_fp) != NULL) {
            fputs(line, fp);
        }
        fclose(tmp_fp);
    }

    fprintf(fp, "\n----------------------------------------------\n");
    fprintf(fp, "End of report\n");
    fclose(fp);

    /* Clean up temp files */
    unlink(temp_sql_file);
    unlink(temp_out_file);

    printf("[INFO] Output written to: %s\n\n", g_output_file);

    /* Show first 30 lines */
    printf("=== First 30 lines of output ===\n\n");
    print_file_head(g_output_file, 30);
    printf("\n...\n(see %s for complete list)\n", g_output_file);

    return 0;
}

/*-------------------------------------------------------------------*/
/* Build SQL query for PTF list                                       */
/*-------------------------------------------------------------------*/

static void build_sql_query(char *sql, size_t sql_len)
{
    char where_clause[512] = "";

    /* Build WHERE clause if products specified */
    if (g_product_count > 0) {
        strcpy(where_clause, "WHERE PTF_PRODUCT_ID IN (");
        for (int i = 0; i < g_product_count; i++) {
            if (i > 0) strcat(where_clause, ", ");
            strcat(where_clause, "'");
            strcat(where_clause, g_products[i]);
            strcat(where_clause, "'");
        }
        strcat(where_clause, ")");
    }

    if (g_verbose) {
        snprintf(sql, sql_len,
            "SELECT "
            "PTF_IDENTIFIER AS PTF_ID, "
            "PTF_PRODUCT_ID AS PRODUCT, "
            "PTF_LOADED_STATUS AS STATUS, "
            "PTF_IPL_ACTION AS IPL_ACTION, "
            "PTF_ACTION_PENDING AS PENDING, "
            "PTF_IPL_REQUIRED AS IPL_REQ, "
            "PTF_CREATION_TIMESTAMP AS CREATED, "
            "PTF_SUPERSEDED_BY_PTF AS SUPERSEDED_BY, "
            "PTF_SAVE_FILE AS SAVE_FILE "
            "FROM QSYS2.PTF_INFO "
            "%s "
            "ORDER BY PTF_PRODUCT_ID, PTF_IDENTIFIER",
            where_clause);
    } else {
        snprintf(sql, sql_len,
            "SELECT "
            "PTF_IDENTIFIER AS PTF_ID, "
            "PTF_PRODUCT_ID AS PRODUCT, "
            "PTF_LOADED_STATUS AS STATUS, "
            "PTF_IPL_ACTION AS IPL_ACTION, "
            "PTF_IPL_REQUIRED AS IPL_REQ "
            "FROM QSYS2.PTF_INFO "
            "%s "
            "ORDER BY PTF_PRODUCT_ID, PTF_IDENTIFIER",
            where_clause);
    }
}

/*-------------------------------------------------------------------*/
/* Build SQL query for PTF summary                                    */
/*-------------------------------------------------------------------*/

static void build_sql_summary(char *sql, size_t sql_len)
{
    char where_clause[512] = "";

    /* Build WHERE clause if products specified */
    if (g_product_count > 0) {
        strcpy(where_clause, "WHERE PTF_PRODUCT_ID IN (");
        for (int i = 0; i < g_product_count; i++) {
            if (i > 0) strcat(where_clause, ", ");
            strcat(where_clause, "'");
            strcat(where_clause, g_products[i]);
            strcat(where_clause, "'");
        }
        strcat(where_clause, ")");
    }

    snprintf(sql, sql_len,
        "SELECT "
        "PTF_PRODUCT_ID AS PRODUCT, "
        "PTF_LOADED_STATUS AS STATUS, "
        "COUNT(*) AS COUNT "
        "FROM QSYS2.PTF_INFO "
        "%s "
        "GROUP BY PTF_PRODUCT_ID, PTF_LOADED_STATUS "
        "ORDER BY PTF_PRODUCT_ID, PTF_LOADED_STATUS",
        where_clause);
}

/*-------------------------------------------------------------------*/
/* Get home directory                                                 */
/*-------------------------------------------------------------------*/

static const char *get_home_dir(void)
{
    const char *home = getenv("HOME");
    if (home != NULL && home[0] != '\0') {
        return home;
    }

    /* Fallback */
    const char *user = getenv("USER");
    if (user != NULL && user[0] != '\0') {
        static char fallback[MAX_PATH_LEN];
        snprintf(fallback, sizeof(fallback), "/QOpenSys/home/%s", user);
        return fallback;
    }

    return "/tmp";
}

/*-------------------------------------------------------------------*/
/* Print first N lines of a file                                      */
/*-------------------------------------------------------------------*/

static void print_file_head(const char *path, int lines)
{
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        printf("(cannot open %s)\n", path);
        return;
    }

    char line[MAX_LINE_LEN];
    int count = 0;

    while (fgets(line, sizeof(line), fp) != NULL && count < lines) {
        printf("%s", line);
        count++;
    }

    fclose(fp);
}

/*-------------------------------------------------------------------*/
/* Usage                                                              */
/*-------------------------------------------------------------------*/

static void usage(const char *prog)
{
    printf("listptf - List PTFs installed on IBM i\n\n");
    printf("Usage: %s [options] [product_id ...]\n\n", prog);
    printf("Options:\n");
    printf("  -o FILE   Write output to FILE (default: ~/ptf_list.txt)\n");
    printf("  -v        Verbose output (more columns)\n");
    printf("  -s        Summary only (count by status)\n");
    printf("  -h        Show this help\n");
    printf("\nArguments:\n");
    printf("  product_id   Product ID to filter (e.g., 5770SS1)\n");
    printf("               Multiple products can be specified\n");
    printf("\nExamples:\n");
    printf("  %s                        # All PTFs\n", prog);
    printf("  %s 5770SS1                # Only OS PTFs\n", prog);
    printf("  %s 5770SS1 5770DG1        # OS and HTTP Server PTFs\n", prog);
    printf("  %s -s                     # Summary counts\n", prog);
    printf("  %s -v -o /tmp/ptfs.txt    # Verbose to specific file\n", prog);
    printf("\nPTF Status values:\n");
    printf("  NOT LOADED              - PTF save file exists but not loaded\n");
    printf("  LOADED                  - Loaded but not applied\n");
    printf("  APPLIED                 - Temporarily applied\n");
    printf("  PERMANENTLY APPLIED     - Permanently applied\n");
    printf("  SUPERSEDED              - Replaced by newer PTF\n");
}
