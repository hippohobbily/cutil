# XCOFF Object Format Research Report

## Executive Summary

This report documents the XCOFF (Extended Common Object File Format) binary format used on AIX and IBM i PASE. It provides practical examples of building various XCOFF objects and demonstrates what analysis commands reveal about each object type.

**Target Environment:** AIX 7.x / IBM i PASE
**Analysis Commands:** `what`, `dump -h`, `dump -T`
**Purpose:** PTF validation, binary comparison, debugging

---

## 1. XCOFF Format Overview

### 1.1 What is XCOFF?

XCOFF (Extended Common Object File Format) is IBM's extension of the Unix COFF format. It is the native executable and object file format for:
- AIX (all versions)
- IBM i PASE (Portable Application Solutions Environment)

### 1.2 XCOFF vs Other Formats

| Platform | Format | Magic Number |
|----------|--------|--------------|
| AIX / PASE | XCOFF | 0x01DF (32-bit), 0x01F7 (64-bit) |
| Linux | ELF | 0x7F454C46 |
| macOS | Mach-O | 0xFEEDFACE |
| Windows | PE/COFF | 0x5A4D (MZ) |

### 1.3 XCOFF File Structure

```
┌────────────────────────────────────────┐
│           File Header (20 bytes)       │  Magic, #sections, timestamp
├────────────────────────────────────────┤
│      Auxiliary Header (72+ bytes)      │  Entry point, section sizes
├────────────────────────────────────────┤
│         Section Headers (40 each)      │  .text, .data, .bss, .loader
├────────────────────────────────────────┤
│            Section Data                │  Raw code and data
├────────────────────────────────────────┤
│          Relocation Entries            │  Address fix-ups
├────────────────────────────────────────┤
│         Line Number Entries            │  Debug info
├────────────────────────────────────────┤
│            Symbol Table                │  Names, addresses, types
├────────────────────────────────────────┤
│            String Table                │  Long symbol names (>8 chars)
└────────────────────────────────────────┘
```

### 1.4 Key Sections

| Section | Purpose |
|---------|---------|
| `.text` | Executable code |
| `.data` | Initialized data |
| `.bss` | Uninitialized data (zero-filled) |
| `.loader` | Dynamic linking info (imports/exports) |
| `.debug` | Debug information |
| `.typchk` | Type checking info |
| `.except` | Exception handling tables |
| `.info` | Comment section |

### 1.5 Symbol Types in Loader Section

| Type | Meaning |
|------|---------|
| `IMP` | Imported symbol (from shared library) |
| `EXP` | Exported symbol (available to others) |
| `DS` | Descriptor (function pointer + TOC) |
| `SECdef` | Section defined |
| `EXTref` | External reference |

---

## 2. Analysis Commands Reference

### 2.1 The `what` Command

**Purpose:** Extract SCCS/RCS identification strings embedded in binaries.

**How it works:** Searches for `@(#)` pattern and extracts the following text.

**Syntax:**
```sh
what <object_file>
```

**Common embedded info:**
- Source file name and version
- Build timestamp
- Compiler version
- Module identification

### 2.2 The `dump -h` Command

**Purpose:** Display section headers.

**Syntax:**
```sh
dump -X64 -h <object_file>    # 64-bit
dump -X32 -h <object_file>    # 32-bit
dump -h <object_file>          # Auto-detect
```

**Output columns:**
- Index: Section number
- Name: Section name (.text, .data, etc.)
- Physical Address: Load address
- Virtual Address: Runtime address
- Size: Section size in bytes
- Offset: File offset
- Flags: Section attributes

### 2.3 The `dump -T` Command

**Purpose:** Display loader section symbol table (imports/exports).

**Syntax:**
```sh
dump -X64 -Tv <object_file>   # Verbose, 64-bit
dump -X64 -T <object_file>    # Standard
```

**Output columns:**
- Index: Symbol number
- Value: Address/offset
- Scn: Section (.text, undef, etc.)
- IMEX: Import/Export indicator
- Sclass: Storage class
- Type: Symbol type
- IMPid: Import source library
- Name: Symbol name

---

## 3. Building Test XCOFF Objects - ABC 123 Examples

The following examples demonstrate building various XCOFF objects and what the analysis commands reveal.

### Example A: Simple "Hello World" Executable

#### A.1 Source Code

**File: `hello.c`**
```c
/*
 * @(#) hello.c 1.0 2024/01/15 - Simple hello world
 * @(#) Author: Test User
 * @(#) Build: Development
 */

#include <stdio.h>

int main(int argc, char *argv[]) {
    printf("Hello, World!\n");
    return 0;
}
```

#### A.2 Build Commands

```sh
# 64-bit executable
xlc -q64 -o hello64 hello.c

# 32-bit executable
xlc -q32 -o hello32 hello.c

# With debug symbols
xlc -g -q64 -o hello64_debug hello.c

# Optimized
xlc -O3 -q64 -o hello64_opt hello.c
```

Alternative with GCC:
```sh
gcc -maix64 -o hello64_gcc hello.c
gcc -maix32 -o hello32_gcc hello.c
```

#### A.3 Expected `what` Output

```
$ what hello64
hello64:
         hello.c 1.0 2024/01/15 - Simple hello world
         Author: Test User
         Build: Development
```

#### A.4 Expected `dump -h` Output

```
$ dump -X64 -h hello64

hello64:

                        ***Section Headers***
[Index] Name      Physical Address  Virtual Address   Size
        Offset    Alignment  Relocation   Line Numbers  Flags

[  0]   .text     0x0000000100000000  0x0000000100000128  0x00000080
        0x00000128     2**2     0x00000000      0x00000000  0x0020

[  1]   .data     0x0000000110000000  0x00000001100001a8  0x00000020
        0x000001a8     2**3     0x00000000      0x00000000  0x0040

[  2]   .bss      0x0000000110000020  0x00000001100001c8  0x00000008
        0x00000000     2**3     0x00000000      0x00000000  0x0080

[  3]   .loader   0x0000000000000000  0x0000000000000000  0x000000c8
        0x000001c8     2**2     0x00000000      0x00000000  0x1000
```

**Interpretation:**
- `.text` (0x0020 = STYP_TEXT): Code section, ~128 bytes
- `.data` (0x0040 = STYP_DATA): Initialized data, ~32 bytes
- `.bss` (0x0080 = STYP_BSS): Uninitialized data, ~8 bytes
- `.loader` (0x1000 = STYP_LOADER): Dynamic linking info

#### A.5 Expected `dump -T` Output

```
$ dump -X64 -Tv hello64

hello64:

                        ***Loader Section***
                        Loader Header Information
VERSION#         #SYMtableENT     #RELOCent        LENidSTR
0x00000001       0x00000005       0x00000002       0x00000040

                        ***Loader Symbol Table Information***
[Index]      Value      Scn     IMEX Sclass   Type           IMPid Name

[0]     0x0000000000000000 undef  IMP     DS EXTref  libc.a(shr_64.o) printf
[1]     0x0000000000000000 undef  IMP     DS EXTref  libc.a(shr_64.o) exit
[2]     0x0000000100000128 .text  EXP     DS SECdef         [noIMid] main

                        ***Import File Strings***
INDEX  PATH                          BASE                MEMBER

0      /usr/lib:/lib
1                                    libc.a              shr_64.o
```

**Interpretation:**
- `printf` and `exit`: Imported from libc.a(shr_64.o)
- `main`: Exported symbol at address 0x100000128
- Import path: /usr/lib:/lib

---

### Example B: Shared Library (.so)

#### B.1 Source Code

**File: `mylib.c`**
```c
/*
 * @(#) mylib.c 2.1 2024/02/01 - Math utility library
 * @(#) Module: MATHUTIL
 * @(#) Copyright (c) 2024 Example Corp
 */

/* Exported function */
int add_numbers(int a, int b) {
    return a + b;
}

/* Exported function */
int multiply_numbers(int a, int b) {
    return a * b;
}

/* Internal helper - not exported */
static int internal_validate(int x) {
    return (x >= 0) ? x : -x;
}

/* Exported with validation */
int safe_add(int a, int b) {
    a = internal_validate(a);
    b = internal_validate(b);
    return add_numbers(a, b);
}
```

**File: `mylib.exp` (Export file)**
```
#! /usr/lib/mylib.a(shr_64.o)
add_numbers
multiply_numbers
safe_add
```

#### B.2 Build Commands

```sh
# Compile to position-independent object
xlc -q64 -c -o mylib.o mylib.c

# Create shared object with exports
xlc -q64 -G -o shr_64.o mylib.o -bE:mylib.exp

# Create archive library
ar -X64 -rv mylib.a shr_64.o

# Alternative: all-in-one
xlc -q64 -G -o mylib.a mylib.c -bE:mylib.exp -bnoentry
```

With GCC:
```sh
gcc -maix64 -shared -o mylib.a mylib.c -Wl,-bE:mylib.exp -Wl,-bnoentry
```

#### B.3 Expected `what` Output

```
$ what mylib.a
mylib.a:
         mylib.c 2.1 2024/02/01 - Math utility library
         Module: MATHUTIL
         Copyright (c) 2024 Example Corp
```

#### B.4 Expected `dump -h` Output

```
$ dump -X64 -h mylib.a

mylib.a[shr_64.o]:

                        ***Section Headers***
[Index] Name      Physical Address  Virtual Address   Size
        Offset    Alignment  Relocation   Line Numbers  Flags

[  0]   .text     0x0000000000000000  0x0000000000000000  0x00000120
        0x0000012c     2**2     0x00000000      0x00000000  0x0020

[  1]   .data     0x0000000000000120  0x0000000000000120  0x00000018
        0x0000024c     2**3     0x00000000      0x00000000  0x0040

[  2]   .loader   0x0000000000000000  0x0000000000000000  0x000000a0
        0x00000264     2**2     0x00000000      0x00000000  0x1000
```

**Interpretation:**
- `.text`: ~288 bytes of code (3 exported + 1 static function)
- `.data`: ~24 bytes (function descriptors)
- No `.bss`: No uninitialized globals

#### B.5 Expected `dump -T` Output

```
$ dump -X64 -Tv mylib.a

mylib.a[shr_64.o]:

                        ***Loader Section***
                        Loader Header Information
VERSION#         #SYMtableENT     #RELOCent        LENidSTR
0x00000001       0x00000003       0x00000000       0x00000030

                        ***Loader Symbol Table Information***
[Index]      Value      Scn     IMEX Sclass   Type           IMPid Name

[0]     0x0000000000000000 .text  EXP     DS SECdef         [noIMid] add_numbers
[1]     0x0000000000000040 .text  EXP     DS SECdef         [noIMid] multiply_numbers
[2]     0x0000000000000080 .text  EXP     DS SECdef         [noIMid] safe_add

                        ***Import File Strings***
INDEX  PATH                          BASE                MEMBER

0      /usr/lib:/lib
```

**Interpretation:**
- 3 exported symbols (from .exp file)
- `internal_validate` is NOT exported (static)
- No imports (self-contained library)
- Symbol addresses show function offsets in .text

---

### Example C: Program Using Shared Library

#### C.1 Source Code

**File: `usemylib.c`**
```c
/*
 * @(#) usemylib.c 1.0 2024/02/15 - Test program for mylib
 * @(#) Depends: mylib.a
 */

#include <stdio.h>

/* External declarations */
extern int add_numbers(int a, int b);
extern int multiply_numbers(int a, int b);
extern int safe_add(int a, int b);

int main(void) {
    int result;

    result = add_numbers(5, 3);
    printf("add_numbers(5, 3) = %d\n", result);

    result = multiply_numbers(4, 7);
    printf("multiply_numbers(4, 7) = %d\n", result);

    result = safe_add(-10, 20);
    printf("safe_add(-10, 20) = %d\n", result);

    return 0;
}
```

#### C.2 Build Commands

```sh
# Link against our library and libc
xlc -q64 -o usemylib usemylib.c -L. -lmylib

# Or explicit path
xlc -q64 -o usemylib usemylib.c ./mylib.a
```

#### C.3 Expected `what` Output

```
$ what usemylib
usemylib:
         usemylib.c 1.0 2024/02/15 - Test program for mylib
         Depends: mylib.a
```

#### C.4 Expected `dump -h` Output

```
$ dump -X64 -h usemylib

usemylib:

                        ***Section Headers***
[Index] Name      Physical Address  Virtual Address   Size
        Offset    Alignment  Relocation   Line Numbers  Flags

[  0]   .text     0x0000000100000000  0x0000000100000128  0x00000100
        0x00000128     2**2     0x00000000      0x00000000  0x0020

[  1]   .data     0x0000000110000000  0x0000000110000228  0x00000040
        0x00000228     2**3     0x00000000      0x00000000  0x0040

[  2]   .bss      0x0000000110000040  0x0000000110000268  0x00000008
        0x00000000     2**3     0x00000000      0x00000000  0x0080

[  3]   .loader   0x0000000000000000  0x0000000000000000  0x00000120
        0x00000268     2**2     0x00000000      0x00000000  0x1000
```

**Interpretation:**
- `.text`: ~256 bytes (main + printf calls)
- `.data`: ~64 bytes (strings, function descriptors)
- `.loader`: Larger than hello world (more imports)

#### C.5 Expected `dump -T` Output

```
$ dump -X64 -Tv usemylib

usemylib:

                        ***Loader Section***
                        Loader Header Information
VERSION#         #SYMtableENT     #RELOCent        LENidSTR
0x00000001       0x00000007       0x00000004       0x00000060

                        ***Loader Symbol Table Information***
[Index]      Value      Scn     IMEX Sclass   Type           IMPid Name

[0]     0x0000000000000000 undef  IMP     DS EXTref  libc.a(shr_64.o) printf
[1]     0x0000000000000000 undef  IMP     DS EXTref  libc.a(shr_64.o) exit
[2]     0x0000000000000000 undef  IMP     DS EXTref  mylib.a(shr_64.o) add_numbers
[3]     0x0000000000000000 undef  IMP     DS EXTref  mylib.a(shr_64.o) multiply_numbers
[4]     0x0000000000000000 undef  IMP     DS EXTref  mylib.a(shr_64.o) safe_add
[5]     0x0000000100000128 .text  EXP     DS SECdef         [noIMid] main

                        ***Import File Strings***
INDEX  PATH                          BASE                MEMBER

0      /usr/lib:/lib:./
1                                    libc.a              shr_64.o
2                                    mylib.a             shr_64.o
```

**Interpretation:**
- Imports from TWO libraries: libc.a and mylib.a
- `printf`, `exit` from libc
- `add_numbers`, `multiply_numbers`, `safe_add` from mylib
- `main` is exported (entry point)

---

### Example 1: Object File (Not Linked)

#### 1.1 Source Code

**File: `module.c`**
```c
/*
 * @(#) module.c 3.0 2024/03/01 - Unlinked module
 */

int global_var = 42;
static int static_var = 100;

extern int external_func(int x);

int module_func(int input) {
    static_var += input;
    return external_func(global_var + static_var);
}
```

#### 1.2 Build Commands

```sh
# Compile only, do not link
xlc -q64 -c -o module.o module.c

# With SCCS string preserved
xlc -q64 -c -qsource -o module.o module.c
```

#### 1.3 Expected `what` Output

```
$ what module.o
module.o:
         module.c 3.0 2024/03/01 - Unlinked module
```

#### 1.4 Expected `dump -h` Output

```
$ dump -X64 -h module.o

module.o:

                        ***Section Headers***
[Index] Name      Physical Address  Virtual Address   Size
        Offset    Alignment  Relocation   Line Numbers  Flags

[  0]   .text     0x0000000000000000  0x0000000000000000  0x00000060
        0x0000008c     2**2     0x000000ec      0x00000000  0x0020

[  1]   .data     0x0000000000000060  0x0000000000000060  0x00000010
        0x000000ec     2**3     0x00000000      0x00000000  0x0040

[  2]   .bss      0x0000000000000070  0x0000000000000070  0x00000004
        0x00000000     2**2     0x00000000      0x00000000  0x0080
```

**Interpretation:**
- NO `.loader` section (not executable, not linked)
- Has relocation entries (0x000000ec) - unresolved references
- `.data` contains `global_var` and `static_var`

#### 1.5 Expected `dump -T` Output

```
$ dump -X64 -T module.o
dump: module.o: 0654-302 object file contains no loader section.
```

**Interpretation:**
- Object files (.o) have no loader section
- Use `dump -t` for full symbol table instead:

```
$ dump -X64 -t module.o

module.o:

                        ***Symbol Table***
[Index]    Value       Scn      Aux  Sclass  Type     Name

[0]       0x00000000    1       1    C_EXT    .()      .module_func
[1]       0x00000000    2       1    C_EXT    ()       module_func
[2]       0x00000000   -1       1    C_EXT    ()       .external_func
[3]       0x00000060    3       1    C_EXT    ()       global_var
[4]       0x00000068    3       1    C_STAT   ()       static_var
```

---

### Example 2: Debug vs Release Build

#### 2.1 Source Code

**File: `debugtest.c`**
```c
/*
 * @(#) debugtest.c 1.0 DEBUG_BUILD
 */
#ifdef NDEBUG
/*
 * @(#) debugtest.c 1.0 RELEASE_BUILD
 */
#endif

#include <stdio.h>
#include <assert.h>

int calculate(int x) {
    assert(x >= 0);

    int result = 0;
    for (int i = 0; i < x; i++) {
        result += i * i;
    }
    return result;
}

int main(void) {
    printf("Result: %d\n", calculate(10));
    return 0;
}
```

#### 2.2 Build Commands

```sh
# Debug build
xlc -g -q64 -o debugtest_debug debugtest.c

# Release build
xlc -O3 -DNDEBUG -q64 -o debugtest_release debugtest.c

# Release with stripping
xlc -O3 -DNDEBUG -q64 -s -o debugtest_stripped debugtest.c
```

#### 2.3 Comparison: `what` Output

**Debug:**
```
$ what debugtest_debug
debugtest_debug:
         debugtest.c 1.0 DEBUG_BUILD
```

**Release:**
```
$ what debugtest_release
debugtest_release:
         debugtest.c 1.0 DEBUG_BUILD
         debugtest.c 1.0 RELEASE_BUILD
```

#### 2.4 Comparison: `dump -h` Output

**Debug (larger):**
```
[  0]   .text     ...  0x00000180   # More code (assert, debug)
[  1]   .data     ...  0x00000040
[  2]   .bss      ...  0x00000008
[  3]   .debug    ...  0x00000800   # Debug info present
[  4]   .loader   ...  0x00000100
```

**Release (smaller, no .debug):**
```
[  0]   .text     ...  0x00000080   # Optimized, smaller
[  1]   .data     ...  0x00000020   # Less data
[  2]   .bss      ...  0x00000008
[  3]   .loader   ...  0x00000080   # Fewer imports (no assert)
```

**Stripped (smallest):**
```
[  0]   .text     ...  0x00000080
[  1]   .data     ...  0x00000020
[  2]   .loader   ...  0x00000080
# No symbol table, no .debug
```

#### 2.5 Comparison: `dump -T` Output

**Debug:**
```
[0]  ... IMP ... libc.a(shr_64.o) printf
[1]  ... IMP ... libc.a(shr_64.o) __assert
[2]  ... IMP ... libc.a(shr_64.o) exit
[3]  ... EXP ... main
[4]  ... EXP ... calculate
```

**Release:**
```
[0]  ... IMP ... libc.a(shr_64.o) printf
[1]  ... IMP ... libc.a(shr_64.o) exit
[2]  ... EXP ... main
# calculate may be inlined, __assert removed
```

---

### Example 3: Multi-file Program

#### 3.1 Source Code

**File: `main3.c`**
```c
/*
 * @(#) main3.c 1.0 2024/03/15 - Main entry point
 */

#include <stdio.h>

extern int process_data(int *data, int len);
extern void print_results(int result);

int main(void) {
    int data[] = {1, 2, 3, 4, 5};
    int result = process_data(data, 5);
    print_results(result);
    return 0;
}
```

**File: `process.c`**
```c
/*
 * @(#) process.c 2.0 2024/03/15 - Data processor
 */

int process_data(int *data, int len) {
    int sum = 0;
    for (int i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum;
}
```

**File: `output.c`**
```c
/*
 * @(#) output.c 1.5 2024/03/15 - Output formatter
 */

#include <stdio.h>

void print_results(int result) {
    printf("The result is: %d\n", result);
}
```

#### 3.2 Build Commands

```sh
# Compile each file
xlc -q64 -c -o main3.o main3.c
xlc -q64 -c -o process.o process.c
xlc -q64 -c -o output.o output.c

# Link together
xlc -q64 -o multifile main3.o process.o output.o
```

#### 3.3 Expected `what` Output

```
$ what multifile
multifile:
         main3.c 1.0 2024/03/15 - Main entry point
         process.c 2.0 2024/03/15 - Data processor
         output.c 1.5 2024/03/15 - Output formatter
```

**Key insight:** All source file versions are embedded in final executable!

#### 3.4 Expected `dump -h` Output

```
$ dump -X64 -h multifile

multifile:

                        ***Section Headers***
[Index] Name      Physical Address  Virtual Address   Size

[  0]   .text     ...  0x00000180    # Combined code from all 3 files
[  1]   .data     ...  0x00000040    # Combined data
[  2]   .bss      ...  0x00000008
[  3]   .loader   ...  0x000000c0
```

#### 3.5 Expected `dump -T` Output

```
$ dump -X64 -Tv multifile

[0]  0x0000000000000000 undef  IMP  DS EXTref  libc.a(shr_64.o) printf
[1]  0x0000000000000000 undef  IMP  DS EXTref  libc.a(shr_64.o) exit
[2]  0x0000000100000128 .text  EXP  DS SECdef  [noIMid] main
[3]  0x0000000100000180 .text  EXP  DS SECdef  [noIMid] process_data
[4]  0x00000001000001c0 .text  EXP  DS SECdef  [noIMid] print_results
```

**Key insight:** All public functions are exported with their final addresses.

---

## 4. Comparison Scenarios

### 4.1 PTF Before/After Comparison

When comparing a binary before and after a PTF:

| Change Type | `what` shows | `dump -h` shows | `dump -T` shows |
|-------------|--------------|-----------------|-----------------|
| Bug fix | New version string | .text size may change | Same symbols |
| New function | Same or new | .text larger | New export |
| Removed function | Same or updated | .text smaller | Symbol removed |
| Dependency change | Same | .loader size change | Different imports |

### 4.2 Using xcoff_snapshot.py

```sh
# Before PTF
./xcoff_snapshot.py snapshot /usr/lib/libcrypto.a -o libcrypto_before.json

# Apply PTF...

# After PTF
./xcoff_snapshot.py snapshot /usr/lib/libcrypto.a -o libcrypto_after.json

# Compare
./xcoff_snapshot.py compare libcrypto_before.json libcrypto_after.json
```

---

## 5. Quick Reference

### 5.1 Command Summary

| Command | Purpose | Works On |
|---------|---------|----------|
| `what file` | Show @(#) strings | Any file |
| `dump -h file` | Section headers | Objects, executables |
| `dump -T file` | Loader symbols | Executables only |
| `dump -t file` | Full symbol table | Objects, executables |
| `dump -H file` | Loader header | Executables only |
| `dump -n file` | Full loader info | Executables only |
| `nm file` | Symbol names | Objects, executables |
| `ldd file` | Dependencies | Executables only |

### 5.2 Common Flags

| Flag | Meaning |
|------|---------|
| `-X64` | Process 64-bit objects |
| `-X32` | Process 32-bit objects |
| `-X32_64` | Process both |
| `-v` | Verbose/symbolic output |
| `-p` | Suppress headers |

### 5.3 Section Flags (Hex)

| Flag | Value | Meaning |
|------|-------|---------|
| STYP_TEXT | 0x0020 | Code section |
| STYP_DATA | 0x0040 | Initialized data |
| STYP_BSS | 0x0080 | Uninitialized data |
| STYP_LOADER | 0x1000 | Loader section |
| STYP_DEBUG | 0x2000 | Debug section |
| STYP_TYPCHK | 0x4000 | Type check |
| STYP_OVRFLO | 0x8000 | Overflow section |

---

## 6. Appendix: Building Test Environment

### 6.1 Setup Script

**File: `setup_xcoff_tests.sh`**
```sh
#!/bin/sh
# Create test XCOFF objects for analysis

# Create test directory
mkdir -p xcoff_tests
cd xcoff_tests

# Create hello.c
cat > hello.c << 'EOF'
/*
 * @(#) hello.c 1.0 2024/01/15 - Simple hello world
 */
#include <stdio.h>
int main(void) {
    printf("Hello, World!\n");
    return 0;
}
EOF

# Create mylib.c
cat > mylib.c << 'EOF'
/*
 * @(#) mylib.c 2.1 2024/02/01 - Math utility library
 */
int add_numbers(int a, int b) { return a + b; }
int multiply_numbers(int a, int b) { return a * b; }
EOF

# Create export file
cat > mylib.exp << 'EOF'
add_numbers
multiply_numbers
EOF

# Build all variants
echo "Building 64-bit hello..."
xlc -q64 -o hello64 hello.c 2>/dev/null || gcc -maix64 -o hello64 hello.c

echo "Building 64-bit shared library..."
xlc -q64 -G -o mylib.a mylib.c -bE:mylib.exp -bnoentry 2>/dev/null || \
    gcc -maix64 -shared -o mylib.a mylib.c -Wl,-bE:mylib.exp -Wl,-bnoentry

echo "Done. Test files created in xcoff_tests/"
ls -la
```

### 6.2 Analysis Script

**File: `analyze_xcoff.sh`**
```sh
#!/bin/sh
# Analyze an XCOFF file

FILE=$1
BITS=${2:-64}

if [ -z "$FILE" ]; then
    echo "Usage: $0 <file> [32|64]"
    exit 1
fi

echo "========================================"
echo "XCOFF Analysis: $FILE"
echo "Mode: ${BITS}-bit"
echo "========================================"

echo ""
echo "--- IDENTIFICATION STRINGS (what) ---"
what "$FILE"

echo ""
echo "--- SECTION HEADERS (dump -h) ---"
dump -X${BITS} -h "$FILE"

echo ""
echo "--- LOADER SYMBOLS (dump -T) ---"
dump -X${BITS} -Tv "$FILE" 2>/dev/null || echo "(No loader section)"

echo ""
echo "--- DEPENDENCIES (ldd) ---"
ldd "$FILE" 2>/dev/null || echo "(Not applicable)"
```

---

## 7. References

- [IBM AIX Documentation - XCOFF Object File Format](https://www.ibm.com/docs/en/aix/7.1.0?topic=formats-xcoff-object-file-format)
- [IBM AIX Documentation - dump Command](https://www.ibm.com/docs/ssw_aix_72/d_commands/dump.html)
- [XCOFF - Wikipedia](https://en.wikipedia.org/wiki/XCOFF)
- AIX Commands Reference, Volume 2

---

*Report generated for XCOFF analysis tooling development*
*Last updated: 2024*
