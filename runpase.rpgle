**FREE
// =====================================================================
// RUNPASE - ILE RPG Program to Run PASE Commands
// =====================================================================
//
// This program demonstrates calling PASE shell commands from ILE RPG
// using the QP2SHELL2 program.
//
// Compile:
//   CRTRPGMOD MODULE(MYLIB/RUNPASE) SRCSTMF('/path/to/runpase.rpgle')
//   CRTPGM PGM(MYLIB/RUNPASE) MODULE(MYLIB/RUNPASE)
//
// Or using CRTBNDRPG:
//   CRTBNDRPG PGM(MYLIB/RUNPASE) SRCSTMF('/path/to/runpase.rpgle')
//
// Run:
//   CALL RUNPASE
//
// =====================================================================

CTL-OPT DFTACTGRP(*NO) ACTGRP(*NEW) OPTION(*SRCSTMT:*NODEBUGIO);

// ---------------------------------------------------------------------
// Prototype for QP2SHELL2
// QP2SHELL2 runs a PASE shell command
// Parameters are null-terminated strings (PASE style)
// ---------------------------------------------------------------------
DCL-PR Qp2Shell2 EXTPGM('QP2SHELL2');
    PathName  POINTER VALUE;       // Path to shell/command
    Argument1 POINTER VALUE OPTIONS(*NOPASS);  // First argument
    Argument2 POINTER VALUE OPTIONS(*NOPASS);  // Second argument
    Argument3 POINTER VALUE OPTIONS(*NOPASS);  // Third argument
    Argument4 POINTER VALUE OPTIONS(*NOPASS);  // Fourth argument
    Argument5 POINTER VALUE OPTIONS(*NOPASS);  // Fifth argument
END-PR;

// ---------------------------------------------------------------------
// Working Variables
// ---------------------------------------------------------------------
DCL-S ShellPath    VARCHAR(256);
DCL-S ShellPathPtr POINTER;
DCL-S Arg1         VARCHAR(256);
DCL-S Arg1Ptr      POINTER;
DCL-S Arg2         VARCHAR(256);
DCL-S Arg2Ptr      POINTER;
DCL-S NullPtr      POINTER INZ(*NULL);

DCL-S OutputFile   VARCHAR(256);
DCL-S PsCommand    VARCHAR(512);

// ---------------------------------------------------------------------
// Main Program
// ---------------------------------------------------------------------

// Display what we're about to do
DSPLY ('Running ps -eaf via PASE...');

// Method 1: Direct call to ps command
// Output goes to job's stdout (QPRINT spool file or terminal)
RunPsSimple();

// Method 2: Redirect output to IFS file
// RunPsToFile();

*INLR = *ON;
RETURN;

// =====================================================================
// RunPsSimple - Run ps -eaf directly
// Output goes to stdout (QPRINT spool file if batch, terminal if interactive)
// =====================================================================
DCL-PROC RunPsSimple;

    // Set up the shell path (use /QOpenSys/usr/bin/sh)
    ShellPath = '/QOpenSys/usr/bin/sh' + X'00';
    ShellPathPtr = %ADDR(ShellPath: *DATA);

    // Arguments: -c "ps -eaf"
    // The shell will execute: sh -c "ps -eaf"
    Arg1 = '-c' + X'00';
    Arg1Ptr = %ADDR(Arg1: *DATA);

    Arg2 = 'ps -eaf' + X'00';
    Arg2Ptr = %ADDR(Arg2: *DATA);

    // Call QP2SHELL2
    // QP2SHELL2(path, arg1, arg2, ...)
    // Arguments are passed to the PASE program
    Qp2Shell2(ShellPathPtr: Arg1Ptr: Arg2Ptr);

    DSPLY ('ps -eaf completed');

END-PROC;

// =====================================================================
// RunPsToFile - Run ps and redirect output to IFS file
// This allows you to read the output in RPG afterwards
// =====================================================================
DCL-PROC RunPsToFile;

    OutputFile = '/tmp/ps_output.txt';

    // Build command: ps -eaf > /tmp/ps_output.txt
    PsCommand = 'ps -eaf > ' + %TRIM(OutputFile) + X'00';

    // Set up shell path
    ShellPath = '/QOpenSys/usr/bin/sh' + X'00';
    ShellPathPtr = %ADDR(ShellPath: *DATA);

    Arg1 = '-c' + X'00';
    Arg1Ptr = %ADDR(Arg1: *DATA);

    Arg2 = PsCommand;
    Arg2Ptr = %ADDR(Arg2: *DATA);

    // Call QP2SHELL2
    Qp2Shell2(ShellPathPtr: Arg1Ptr: Arg2Ptr);

    DSPLY ('Output written to ' + %TRIM(OutputFile));

    // Now you could read the file using IFS APIs or SQL
    // Example: SELECT * FROM TABLE(QSYS2.IFS_READ('/tmp/ps_output.txt'))

END-PROC;
