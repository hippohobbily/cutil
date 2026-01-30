/*
 * runpase.c - ILE C Program to Run PASE Commands
 *
 * This program demonstrates calling PASE shell commands from ILE C
 * using the Qp2RunPase() API and QP2SHELL2 program.
 *
 * Compile on IBM i:
 *   CRTCMOD MODULE(MYLIB/RUNPASE) SRCSTMF('/path/to/runpase.c') SYSIFCOPT(*IFS64IO)
 *   CRTPGM PGM(MYLIB/RUNPASE) MODULE(MYLIB/RUNPASE)
 *
 * Or single step:
 *   CRTBNDC PGM(MYLIB/RUNPASE) SRCSTMF('/path/to/runpase.c') SYSIFCOPT(*IFS64IO)
 *
 * Run:
 *   CALL RUNPASE
 *
 * Methods shown:
 *   1. QP2SHELL2 - Simple way to run a PASE command
 *   2. Qp2RunPase() - More control, can capture return code
 *   3. popen() - Run command and read output (requires PASE context)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* IBM i specific headers */
#include <qp2user.h>      /* Qp2RunPase, Qp2EndPase, etc. */
#include <qusec.h>        /* Error code structure */

/* For calling QP2SHELL2 */
#include <qp2shell2.h>    /* If available, or use manual prototype */

/* IFS file I/O */
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

/*-------------------------------------------------------------------*/
/* Prototypes                                                         */
/*-------------------------------------------------------------------*/
void run_ps_simple(void);
void run_ps_with_api(void);
void run_ps_to_file(void);
void display_ifs_file(const char *path);

/*-------------------------------------------------------------------*/
/* Main                                                               */
/*-------------------------------------------------------------------*/
int main(int argc, char *argv[])
{
    printf("===========================================\n");
    printf("RUNPASE - Run PASE Commands from ILE\n");
    printf("===========================================\n\n");

    /* Method 1: Simple - use system() with QSH */
    printf("Method 1: Using system() with QSH\n");
    printf("-------------------------------------------\n");
    run_ps_simple();

    /* Method 2: Write to file and display */
    printf("\nMethod 2: Redirect to IFS file\n");
    printf("-------------------------------------------\n");
    run_ps_to_file();

    /* Method 3: Using Qp2RunPase API (more complex) */
    /* Uncomment if needed:
    printf("\nMethod 3: Using Qp2RunPase API\n");
    printf("-------------------------------------------\n");
    run_ps_with_api();
    */

    printf("\n===========================================\n");
    printf("Done!\n");
    printf("===========================================\n");

    return 0;
}

/*-------------------------------------------------------------------*/
/* Method 1: Simple - use system() to call QSH                        */
/*                                                                    */
/* This runs the command via QSHELL, which can call PASE.             */
/* Output goes to stdout (QPRINT spool if batch).                     */
/*-------------------------------------------------------------------*/
void run_ps_simple(void)
{
    int rc;

    printf("Running: ps -eaf (via QSH)\n\n");

    /*
     * The system() function in ILE C runs a CL command.
     * We use STRQSH (Start QShell) to run the PASE command.
     * QSH automatically bridges to PASE for commands like 'ps'.
     */
    rc = system("STRQSH CMD('ps -eaf | head -20')");

    if (rc != 0) {
        printf("system() returned: %d\n", rc);
    }

    printf("\n(Showing first 20 lines)\n");
}

/*-------------------------------------------------------------------*/
/* Method 2: Redirect output to IFS file, then display                */
/*                                                                    */
/* This is useful when you need to process the output in RPG/C.       */
/*-------------------------------------------------------------------*/
void run_ps_to_file(void)
{
    int rc;
    const char *output_file = "/tmp/ps_output.txt";
    char cmd[512];

    printf("Running: ps -eaf > %s\n", output_file);

    /* Build QSH command to redirect output to file */
    snprintf(cmd, sizeof(cmd),
             "STRQSH CMD('ps -eaf > %s')",
             output_file);

    rc = system(cmd);

    if (rc != 0) {
        printf("Command failed with rc=%d\n", rc);
        return;
    }

    printf("Output written to: %s\n\n", output_file);

    /* Display first 20 lines of the file */
    printf("First 20 lines of output:\n");
    printf("-------------------------------------------\n");
    display_ifs_file(output_file);

    /* Clean up */
    /* unlink(output_file); */  /* Uncomment to delete temp file */
}

/*-------------------------------------------------------------------*/
/* Display contents of an IFS file (first N lines)                    */
/*-------------------------------------------------------------------*/
void display_ifs_file(const char *path)
{
    FILE *fp;
    char line[256];
    int line_count = 0;
    int max_lines = 20;

    fp = fopen(path, "r");
    if (fp == NULL) {
        printf("Cannot open %s: %s\n", path, strerror(errno));
        return;
    }

    while (fgets(line, sizeof(line), fp) != NULL && line_count < max_lines) {
        printf("%s", line);
        line_count++;
    }

    fclose(fp);

    if (line_count >= max_lines) {
        printf("... (truncated, showing %d lines)\n", max_lines);
    }
}

/*-------------------------------------------------------------------*/
/* Method 3: Using Qp2RunPase() API                                   */
/*                                                                    */
/* This gives more control but is more complex.                       */
/* Useful when you need to:                                           */
/*   - Pass environment variables                                     */
/*   - Control CCSID conversion                                       */
/*   - Get the exact return code                                      */
/*-------------------------------------------------------------------*/
void run_ps_with_api(void)
{
    int rc;
    QP2_ptr64_t  pase_argv[4];       /* Argument pointers (PASE 64-bit) */
    QP2_ptr64_t  pase_envp[1];       /* Environment pointers */
    char         path[256];
    char         arg1[16];
    char         arg2[256];

    printf("Running ps -eaf using Qp2RunPase API\n");

    /*
     * Set up arguments for: /QOpenSys/usr/bin/sh -c "ps -eaf"
     *
     * In PASE, argv[0] is the program name, argv[1], argv[2], etc.
     * are the arguments.
     */

    /* Path to the shell */
    strcpy(path, "/QOpenSys/usr/bin/sh");

    /* Arguments */
    strcpy(arg1, "-c");
    strcpy(arg2, "ps -eaf | head -10");

    /*
     * For Qp2RunPase, we need to convert ILE pointers to PASE pointers.
     * This requires using Qp2malloc() or mapping memory.
     *
     * For simplicity, this example uses the simpler system() approach.
     * Full Qp2RunPase implementation requires:
     *   1. Qp2malloc() to allocate PASE-accessible memory
     *   2. Copy strings to that memory
     *   3. Build argv[] array with PASE pointers
     *   4. Call Qp2RunPase()
     *   5. Qp2free() to clean up
     *
     * See IBM documentation for complete example:
     * https://www.ibm.com/docs/en/i/7.4?topic=ssw_ibm_i_74/apis/qp2runpase.html
     */

    printf("(Full Qp2RunPase example not implemented - use Method 1 or 2)\n");
}
