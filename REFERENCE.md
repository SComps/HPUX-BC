# HP-UX BASIC Compiler (bcc) — Programmer's Reference Manual

**Version 1.0 — HP-UX 11i V1 (PA-RISC 1.1 32-bit) & POSIX/Linux**

---

## Table of Contents

1. [Overview](#1-overview)
2. [Installation and Build](#2-installation-and-build)
3. [Compiler Invocation](#3-compiler-invocation)
4. [Language Structure](#4-language-structure)
5. [Data Types](#5-data-types)
6. [Variables and Declaration](#6-variables-and-declaration)
7. [Operators and Expressions](#7-operators-and-expressions)
8. [Control Flow](#8-control-flow)
9. [Subroutines and Functions](#9-subroutines-and-functions)
10. [Arrays](#10-arrays)
11. [User-Defined Types](#11-user-defined-types)
12. [Console I/O](#12-console-io)
13. [File I/O](#13-file-io)
14. [Error Handling](#14-error-handling)
15. [Built-in Functions — Strings](#15-built-in-functions--strings)
16. [Built-in Functions — Math](#16-built-in-functions--math)
17. [Built-in Functions — Conversion](#17-built-in-functions--conversion)
18. [Built-in Functions — File Status](#18-built-in-functions--file-status)
19. [Runtime Error Codes](#19-runtime-error-codes)
20. [Compiler Internals](#20-compiler-internals)
21. [Limitations and Known Differences](#21-limitations-and-known-differences)

---

## 1. Overview

`bcc` is a structured BASIC compiler for HP-UX 11i V1 on PA-RISC 1.1.  It
translates `.bas` source files into C, then invokes `gcc 4.2` to produce a
native PA-RISC 32-bit binary.

The dialect is modelled on **QuickBASIC / FreeBASIC** (structured, no
required line numbers) with the following additions and omissions:

- **Added:** `TRY / CATCH / END TRY` structured exception handling.
- **Omitted:** `RESUME` (re-execute faulting statement), `PEEK`/`POKE`,
  inline assembly, object-oriented features, namespaces, preprocessor macros.

The runtime library `libbasic.a` is linked into every compiled program and
provides strings, I/O, file access, math, and error handling.

---

## 2. Installation and Build

### Prerequisites

| Tool | Minimum version | Typical HP-UX path |
|---|---|---|
| gcc | 4.2 | `/usr/local/pa11_32/bin/gcc` |
| gmake | 3.x | `/usr/local/pa11_32/bin/gmake` |
| ar | any | `/usr/bin/ar` |
| ranlib | any | `/usr/bin/ranlib` |

### Build steps

```sh
chmod +x configure
./configure                    # detects tools, writes config.mk
gmake                          # builds bcc and libbasic.a
gmake install                  # installs to PREFIX (auto-detected)
```

`configure` auto-selects `PREFIX`:

1. `/usr/local/pa11_32` — if the directory exists and is writable (the HP-UX
   porting-centre layout; preferred).
2. `/usr/local` — if writable (requires root).
3. `$HOME/local` — last resort.

Override any of these:

```sh
./configure --prefix=/opt/bcc --cc=/usr/local/pa11_32/bin/gcc
```

### Installed files

```
$PREFIX/bin/bcc                        compiler driver
$PREFIX/bin/bcc-ide                    full-screen terminal IDE
$PREFIX/lib/libbasic.a                 runtime static library
$PREFIX/include/basic_runtime.h        runtime header
$PREFIX/share/hpux-bc/ide.bas          IDE BASIC source
$PREFIX/share/doc/hpux-bc/REFERENCE.md reference manual
```

### Building the IDE and HP-UX SD-UX Depot

To build and install the terminal IDE (`bcc-ide`):

```sh
cd ide
gmake
gmake install
```

Launch the IDE optionally passing a `.bas` file on the command line:

```sh
bcc-ide [file.bas]
```

On HP-UX, you can also package the compiler, runtime, IDE, and documentation into a single SD-UX tape depot (`.depot`) with dependency declarations (`gcc` and `ncurses`):

```sh
gmake depot                    # creates hpux-bc-1.0.depot
/usr/sbin/swinstall -s $(pwd)/hpux-bc-1.0.depot HPUX-BC
```

### Pointing bcc at the runtime

After install, `bcc` locates `libbasic.a` and `basic_runtime.h` in the following order:

1. The `BCCRUNTIME` environment variable (if set).
2. The directory in `argv[0]` when invoked with an explicit path (e.g. `./bcc`).
3. The current working directory if `./libbasic.a` is present, or the compiled-in `$PREFIX` directory configured by `./configure`.

---

## 3. Compiler Invocation

```
bcc [options] file.bas
```

### Options

| Option | Meaning |
|---|---|
| `-o outfile` | Name of the output binary (default: basename of `.bas` file) |
| `-c` | Emit C only — write `.c` file, do not invoke gcc |
| `-v` | Verbose — print pipeline steps and keep temp `.c` file |
| `--lex` | Debug — dump token stream to stdout and exit |
| `--parse` | Debug — dump AST to stdout and exit |
| `-I path` | Add an include search path (passed through to gcc) |
| `-O0` / `-O1` / `-O2` | Optimisation level passed to gcc (default `-O2`) |

### Examples

```sh
bcc hello.bas                  # compile and link → ./hello
bcc -o myapp hello.bas         # compile → ./myapp
bcc -c hello.bas               # emit hello.c only
bcc -v hello.bas               # verbose; keeps /tmp/bcc_PID_N.c
bcc --lex hello.bas            # dump tokens
bcc --parse hello.bas          # dump AST
```

### Exit codes

| Code | Meaning |
|---|---|
| 0 | Success |
| 1 | Compile or link error |
| 2 | Usage error |

---

## 4. Language Structure

### Source files

Source files have the extension `.bas`.  The compiler processes one file per
invocation.  The file is case-insensitive: `IF`, `If`, and `if` are all
equivalent.  The compiler folds all identifiers and keywords to uppercase
internally.

### Statements and line structure

BASIC is **line-oriented**.  A newline terminates a statement.  Multiple
statements may appear on one line separated by `:`.

```basic
a = 1 : b = 2 : PRINT a + b
```

### Line continuation

A line ending with `_` (underscore) continues onto the next line:

```basic
x = (longVariableName + _
     anotherLongName) * 3
```

### Comments

```basic
REM This is a comment
' This is also a comment
a = 1  ' inline comment
```

### Labels

A bare identifier followed by `:` at the start of a statement defines a
label.  Labels are used by `GOTO`, `GOSUB`, and `ON ERROR GOTO`.

```basic
StartLoop:
    PRINT i
    i = i + 1
    IF i < 10 THEN GOTO StartLoop
```

### Program structure

A BASIC source file contains:
- Optional `OPTION BASE` statement (must be first executable statement).
- `TYPE` definitions (can appear anywhere at module level).
- `SUB` and `FUNCTION` definitions.
- Module-level statements (the main program body).

Execution begins at the first module-level statement.  `SUB` and `FUNCTION`
bodies are skipped during module-level execution.

---

## 5. Data Types

| BASIC type | Sigil | C equivalent | Size (PA-RISC 32) | Range |
|---|---|---|---|---|
| `INTEGER` | `%` | `int` | 4 bytes | −2,147,483,648 to 2,147,483,647 |
| `LONG` | `&` | `long` | 4 bytes | −2,147,483,648 to 2,147,483,647 |
| `SINGLE` | `!` | `float` | 4 bytes | ±3.4 × 10³⁸, ~7 decimal digits |
| `DOUBLE` | `#` | `double` | 8 bytes | ±1.8 × 10³⁰⁸, ~15 decimal digits |
| `STRING` | `$` | `BString` | heap | Variable length |

> **Note:** On PA-RISC 1.1 with gcc 4.2 in the default 32-bit ABI, `int` and
> `long` are both 32 bits.  `LONG` and `INTEGER` are therefore identical in
> range.

### Implicit typing

If a variable is used without a `DIM` declaration, its type is determined by
its sigil suffix.  If no sigil is present, the type defaults to **SINGLE**.

```basic
x = 3.14          ' SINGLE (default, no sigil)
n% = 42           ' INTEGER
name$ = "Alice"   ' STRING
```

### Literal syntax

| Type | Examples |
|---|---|
| Integer | `42`, `-7`, `32767` |
| Long | `100000&`, `-32768&` |
| Single | `3.14`, `1.5E2` |
| Double | `3.14159265358979#`, `1.5D2` |
| String | `"Hello"`, `""` (empty), `"say ""hi"""` (embedded quote = `""`) |

---

## 6. Variables and Declaration

### DIM statement

`DIM` declares variables explicitly.  This is optional (variables can be
used without declaration) but is strongly recommended.

```basic
DIM name AS type
DIM name[(dims)] AS type
DIM name[sigil][(dims)]
```

Examples:

```basic
DIM count AS INTEGER
DIM total AS DOUBLE
DIM name AS STRING
DIM scores(10) AS INTEGER           ' static array, indices 0..10
DIM matrix(1 TO 5, 1 TO 5) AS DOUBLE
DIM buf(n) AS INTEGER               ' dynamic array (n is a variable)
```

Multiple declarations on one line:

```basic
DIM a AS INTEGER, b AS INTEGER, c AS DOUBLE
```

### DIM SHARED

`DIM SHARED` declares a module-level scalar or array variable visible inside
all `SUB`s and `FUNCTION`s without being passed as a parameter.

```basic
DIM SHARED globalCounter AS INTEGER
DIM SHARED lookupTable(100) AS STRING
```

### Variable scope

| Context | Scope |
|---|---|
| Module level | Global — visible everywhere |
| Inside SUB/FUNCTION | Local — visible only within that SUB/FUNCTION |
| DIM SHARED | Module-global — visible in all SUBs/FUNCTIONs |

### OPTION BASE

Sets the default lower bound for arrays declared without explicit bounds.

```basic
OPTION BASE 0    ' arrays start at index 0 (default)
OPTION BASE 1    ' arrays start at index 1
```

Must appear before any `DIM` statement.

---

## 7. Operators and Expressions

### Arithmetic operators

| Operator | Meaning | Example |
|---|---|---|
| `+` | Addition | `a + b` |
| `-` | Subtraction / unary negation | `a - b`, `-x` |
| `*` | Multiplication | `a * b` |
| `/` | Floating-point division | `7 / 2` → `3.5` |
| `\` | Integer division (truncates toward zero) | `7 \ 2` → `3` |
| `MOD` | Modulo (remainder) | `7 MOD 3` → `1` |
| `^` | Exponentiation (right-associative) | `2 ^ 10` → `1024` |

### String operator

| Operator | Meaning | Example |
|---|---|---|
| `&` | String concatenation | `"Hello" & " " & "World"` |

### Relational operators

All relational operators return `−1` (true) or `0` (false) as INTEGER.

| Operator | Meaning |
|---|---|
| `=` | Equal |
| `<>` | Not equal |
| `<` | Less than |
| `>` | Greater than |
| `<=` | Less than or equal |
| `>=` | Greater than or equal |

String comparisons use lexicographic (ASCII) order.

### Logical operators

| Operator | Meaning | Note |
|---|---|---|
| `AND` | Logical/bitwise AND | Short-circuit for booleans, bitwise for integers |
| `OR` | Logical/bitwise OR | |
| `NOT` | Logical/bitwise NOT | |
| `XOR` | Exclusive OR | |

### Operator precedence (highest to lowest)

| Priority | Operators |
|---|---|
| 1 (highest) | `^` (right-associative) |
| 2 | Unary `-`, unary `+` |
| 3 | `*`, `/`, `\`, `MOD` |
| 4 | `+`, `-` |
| 5 | `&` (string concat) |
| 6 | `=`, `<>`, `<`, `>`, `<=`, `>=` |
| 7 | `NOT` |
| 8 | `AND` |
| 9 | `OR`, `XOR` |
| 10 (lowest) | (assignment) |

### Type coercion in expressions

- Integer `op` Integer → Integer (except `/` which yields SINGLE).
- Any Float operand → both sides promoted to the wider float type.
- String `&` String → String.
- Comparison of mismatched numeric types → both promoted to DOUBLE.

---

## 8. Control Flow

### IF / THEN / ELSE

**Single-line form** (no `END IF`):

```basic
IF condition THEN statement
IF condition THEN statement ELSE statement
```

**Multi-line form:**

```basic
IF condition THEN
    statements
ELSEIF condition THEN
    statements
ELSE
    statements
END IF
```

`ELSEIF` may be repeated any number of times.

### FOR / NEXT

```basic
FOR var = start TO limit [STEP step]
    statements
NEXT [var]
```

- `step` defaults to `1` if omitted.
- A negative `step` counts downward: `FOR i = 10 TO 1 STEP -1`.
- `NEXT var` closes the loop for `var`; the variable name is optional.
- `EXIT FOR` exits the loop immediately.

```basic
FOR i = 1 TO 10
    IF i = 5 THEN EXIT FOR
    PRINT i
NEXT i
```

### WHILE / WEND

```basic
WHILE condition
    statements
WEND
```

### DO / LOOP

Four forms:

```basic
DO                     ' infinite loop
    statements
LOOP

DO WHILE condition     ' test at top
    statements
LOOP

DO UNTIL condition     ' test at top (loop while NOT condition)
    statements
LOOP

DO                     ' test at bottom
    statements
LOOP WHILE condition

DO                     ' test at bottom
    statements
LOOP UNTIL condition
```

`EXIT DO` exits the loop immediately.

### SELECT CASE

```basic
SELECT CASE expression
    CASE value
        statements
    CASE value1, value2, value3
        statements
    CASE value1 TO value2
        statements
    CASE IS > value
        statements
    CASE ELSE
        statements
END SELECT
```

`IS` comparison operators: `=`, `<>`, `<`, `>`, `<=`, `>=`.

The `expression` can be numeric or string.  Cases are tested top to bottom;
the first match executes.

### GOTO

```basic
GOTO LabelName
```

Jumps unconditionally to the named label.

### GOSUB / RETURN

```basic
GOSUB LabelName
...
LabelName:
    statements
RETURN
```

`GOSUB` calls a subroutine inline (no separate scope).  `RETURN` resumes
execution after the `GOSUB` statement.

> **Implementation note:** The compiler emits GOSUB as a `goto`/label pair
> within the enclosing function.  GOSUB subroutines share the caller's local
> variables.

### END

Terminates the program immediately.

---

## 9. Subroutines and Functions

### SUB

```basic
SUB Name([paramlist])
    statements
END SUB
```

- Called with: `Name arg1, arg2` or `CALL Name(arg1, arg2)`.
- Does not return a value.
- `EXIT SUB` returns immediately.

### FUNCTION

```basic
FUNCTION Name([paramlist]) [AS returntype]
    statements
    Name = returnvalue     ' set return value by assigning to function name
END FUNCTION
```

- Returns a value; called as part of an expression: `x = Name(a, b)`.
- `EXIT FUNCTION` returns immediately with the current value of `Name`.
- If the return type is `STRING`, the function returns a string.

### Parameter lists

```basic
SUB Example(a AS INTEGER, BYVAL b AS DOUBLE, BYREF c AS STRING)
```

| Passing mode | Meaning |
|---|---|
| `BYVAL` (default for numeric types) | A copy is passed; the caller's variable is not modified |
| `BYREF` (default for strings and arrays) | A reference is passed; assignments inside modify the caller's variable |

Array parameters are always passed by reference.  Specify with `()`:

```basic
SUB FillArray(arr() AS INTEGER, n AS INTEGER)
    DIM i AS INTEGER
    FOR i = 0 TO n-1
        arr(i) = i * 2
    NEXT i
END SUB
```

### Recursion

Functions and subroutines may call themselves recursively.

```basic
FUNCTION Factorial(n AS INTEGER) AS LONG
    IF n <= 1 THEN
        Factorial = 1
    ELSE
        Factorial = n * Factorial(n - 1)
    END IF
END FUNCTION
```

---

## 10. Arrays

### Static arrays

Bounds are compile-time constants.  Emitted as plain C arrays.

```basic
DIM a(10) AS INTEGER              ' indices 0 to 10  (11 elements)
DIM b(1 TO 5) AS DOUBLE           ' indices 1 to 5
DIM grid(3, 3) AS INTEGER         ' 4×4 grid (indices 0..3 each)
DIM cube(1 TO 2, 1 TO 3, 1 TO 4) AS SINGLE
```

### Dynamic arrays

Bounds involve variables.  Allocated on the heap using `BArray`.

```basic
DIM n AS INTEGER
INPUT n
DIM scores(n) AS INTEGER          ' allocated at runtime
```

### REDIM

Resizes a heap-allocated array.  The array must have been declared with
`DIM` using variable bounds.

```basic
REDIM scores(newSize) AS INTEGER          ' resize; existing data lost
REDIM PRESERVE scores(newSize) AS INTEGER ' resize; preserve existing data
```

`PRESERVE` copies as many elements as fit in both the old and new bounds.
Only the last dimension may change bounds when using `PRESERVE`.

### Array access

```basic
a(i) = 42
PRINT a(i)
grid(row, col) = 1
```

Indices outside the declared bounds trigger runtime error 9
(Subscript out of range).

### Passing arrays to SUBs

```basic
DIM data(100) AS INTEGER
FillArray data(), 100
```

---

## 11. User-Defined Types

### TYPE definition

```basic
TYPE TypeName
    fieldName AS FieldType
    ...
END TYPE
```

Example:

```basic
TYPE Point
    x AS DOUBLE
    y AS DOUBLE
END TYPE

TYPE Rectangle
    topLeft AS Point
    width   AS DOUBLE
    height  AS DOUBLE
END TYPE
```

### Declaring and using TYPE variables

```basic
DIM p AS Point
p.x = 3.14
p.y = 2.72
PRINT p.x; p.y

DIM r AS Rectangle
r.topLeft.x = 0.0
r.topLeft.y = 0.0
r.width = 100.0
r.height = 50.0
```

TYPE variables may be passed to SUBs/FUNCTIONs by value or by reference.
Arrays of user types are supported:

```basic
DIM points(10) AS Point
points(1).x = 1.0
points(1).y = 2.0
```

---

## 12. Console I/O

### PRINT

```basic
PRINT                                ' print blank line
PRINT expr                           ' print value and newline
PRINT expr; expr                     ' print values, no space between
PRINT expr, expr                     ' print values at tab stops (14-char intervals)
PRINT expr;                          ' print value, no trailing newline
```

Number formatting: positive numbers are preceded by a space; negative numbers
by a minus sign. `SINGLE` values print up to 7 significant digits (`%.7g`) and
`DOUBLE` values (as well as `STR$`) print up to 15 significant digits (`%.15g`)
before switching to scientific notation.

**PRINT USING:**

```basic
PRINT USING formatString; expr [, expr ...] [;]
PRINT USING formatString; expr [; expr ...] [;]
```

A trailing semicolon (`;`) suppresses the newline at the end of `PRINT USING`.
Numeric format fields accept both numeric expressions and numeric strings (such
as `TRIM$(STR$(n))`).

Format string characters:

| Char | Meaning |
|---|---|
| `#` | Digit placeholder (right-justified, space-padded) |
| `,` | Thousands separator inside a `#` mask (e.g. `###,###,###`) |
| `.` | Decimal point position |
| `+` | Show sign (leading or trailing `+` for positive, `-` for negative) |
| `-` | Trailing minus for negative numbers (space for positive) |
| `$` | Fixed leading dollar sign prefix |
| `$$` | Floating dollar sign (placed immediately left of the first digit) |
| `**` | Fill leading unused digit positions with `*` (counts as 2 positions) |
| `!` | Single-character string field (first character of string) |
| `\ \` | Fixed-width string field (width = spaces between `\` + 2) |
| `&` | Variable-width string field (full string) |
| `^^^^` | Scientific notation suffix (`E+nn`) |

> **GW-BASIC / QuickBASIC 4.5 `PRINT USING` incompatibilities:**
> - **`**$` combined prefix:** `$**###.##` is supported, but `**$###.##` (asterisk fill followed by floating `$`) is not combined as a 3-character prefix.
> - **`_` literal escape character:** Underscore (`_`) does not escape the following format character (`_#`, `_!`, `_&`); it is printed as a literal `_`.
> - **`%` overflow indicator:** Numbers wider than the `#` mask are printed in full without prepending a leading `%` character.
> - **Format cycling:** A single `PRINT USING` statement makes one pass over `formatString`; if more expressions are supplied than format fields in `formatString`, the extra expressions are ignored rather than repeating `formatString`.

### INPUT

```basic
INPUT var
INPUT var1, var2, var3
INPUT "Prompt"; var
INPUT "Prompt", var
```

Reads one or more comma-separated values from stdin.

* **No prompt** — prints `? ` then waits for input.
* **`"Prompt"; var`** — prints the prompt string followed by `? ` (semicolon
  separator), then waits.
* **`"Prompt", var`** — prints the prompt string with **no** `? ` (comma
  separator), then waits.  Use this when the prompt already ends with a
  natural cue such as a colon or space.

Strings do not need quoting unless they contain commas.

### LINE INPUT

```basic
LINE INPUT var$
LINE INPUT "Prompt: "; var$
LINE INPUT "Prompt: ", var$
```

Reads an entire line (no comma splitting) into a single string variable.

* **No prompt** — reads silently (no `? `).
* **`"Prompt"; var$`** — prints the prompt followed by `? `, then reads.
* **`"Prompt", var$`** — prints the prompt with no `? `, then reads.

### CLS

```basic
CLS
```

Clears the terminal screen and moves the cursor to row 1, column 1.
Uses ANSI/VT100 escape sequences (`ESC[2J ESC[H`).

### LOCATE

```basic
LOCATE row [, col]
```

Moves the cursor to the given 1-based `row` and `col`.  If `col` is
omitted it defaults to 1.

```basic
LOCATE 5, 1
PRINT "Hello at row 5"
```

### COLOR

```basic
COLOR foreground [, background]
```

Sets the terminal foreground and optionally background colour using ANSI
codes.  Colour numbers follow the QuickBASIC convention:

| Number | Colour |
|---|---|
| 0 | Black |
| 1 | Dark Red |
| 2 | Dark Green |
| 3 | Dark Yellow / Brown |
| 4 | Dark Blue |
| 5 | Dark Magenta |
| 6 | Dark Cyan |
| 7 | Light Gray |
| 8 | Dark Gray |
| 9 | Bright Red |
| 10 | Bright Green |
| 11 | Bright Yellow |
| 12 | Bright Blue |
| 13 | Bright Magenta |
| 14 | Bright Cyan |
| 15 | Bright White |

Background colours use numbers 0-7 only.  Pass -1 for background to
leave it unchanged.

```basic
COLOR 14, 1        ' bright yellow on dark red
PRINT "Warning!"
COLOR 7            ' restore light gray, leave background
```

### BGCOLOR

```basic
BGCOLOR background
```

Sets the terminal **background colour only**, leaving the foreground
unchanged.  Uses the same 0-7 colour numbers as `COLOR`.

```basic
BGCOLOR 1          ' dark red background
PRINT "Error text"
BGCOLOR 0          ' back to black
```

### LPRINT / LPRINT USING

```basic
LPRINT                                ' print blank line to printer
LPRINT expr                           ' print value + newline to printer
LPRINT expr; expr                     ' print without space between
LPRINT expr,                          ' print without trailing newline
LPRINT USING formatString; expr, ...  ' formatted print to printer
LPRINT OPEN ["printername"]           ' set printer destination
LPRINT CLOSE                          ' flush and submit the print job
```

All `LPRINT` output is buffered in a pipe to the HP-UX `lp` spooler.
The job is submitted when `LPRINT CLOSE` is executed or when the program
exits normally.

**Printer selection** (first match wins):
1. `LPRINT OPEN "printername"` — explicit LP destination name
2. `BCCPRINTER` environment variable — e.g. `export BCCPRINTER=laser1`
3. System default printer (HP-UX `lp` with no `-d` flag)

```basic
' Print to the default printer
LPRINT "Report title"
LPRINT USING "###.##"; totalAmount
LPRINT CLOSE

' Print to a named printer
LPRINT OPEN "laser2"
LPRINT "Hello from BASIC"
LPRINT CLOSE
```

> **HP-UX note:** `LPRINT` uses `popen("lp -d printername -", "w")` to
> open the spooler pipe.  Output is sent as plain text.  The print job
> is not submitted until `LPRINT CLOSE` or program exit.  If the named
> printer does not exist, `lp` will report an error to stderr but the
> BASIC program will continue without crashing.

### SHELL

```basic
SHELL                         ' spawn an interactive shell
SHELL commandString$          ' run command, discard output
```

Execute a shell command using the system shell (`/bin/sh`, or the
value of the `SHELL` environment variable).  The program waits for
the command to finish.  If no argument is given an interactive shell
is spawned; the user types `exit` to return to the BASIC program.

The exit code of the command is stored internally and can be
retrieved with the `ERRLEVEL` function.

```basic
SHELL "cp /tmp/data.txt /tmp/backup.txt"
IF ERRLEVEL <> 0 THEN PRINT "copy failed"
```

### SHELL$(cmd$)

```basic
result$ = SHELL$(commandString$)
```

Run `commandString$` and return its standard output as a single
string (newlines included).  Implemented with `popen()`.

```basic
DIM osname AS STRING
osname = SHELL$("uname -s")
PRINT "Running on: "; osname

DIM listing AS STRING
listing = SHELL$("ls -1 /tmp | head -5")
PRINT listing
```

### ERRLEVEL

```basic
code% = ERRLEVEL
```

Returns (as an `INTEGER`) the exit code of the most recently
executed `SHELL` command.  Returns `0` if no `SHELL` has been
executed or if the last `SHELL` succeeded.

```basic
SHELL "test -f /etc/passwd"
IF ERRLEVEL = 0 THEN PRINT "/etc/passwd exists"
```

### INKEY$

```basic
k$ = INKEY$
```

Non-blocking keyboard read.  Returns immediately.

| Return value          | Meaning                          |
|-----------------------|----------------------------------|
| `""` (empty string)   | No key is currently pressed      |
| Single character      | Normal printable or control key  |
| `CHR$(0) + scancode`  | Extended key (arrow / function)  |

Extended key scan codes (QuickBASIC convention):

| Key        | `ASC(MID$(k$,2,1))` |
|------------|---------------------|
| Up arrow   | 72                  |
| Down arrow | 80                  |
| Left arrow | 75                  |
| Right arrow| 77                  |
| F1         | 59                  |
| F2         | 60                  |
| F3         | 61                  |
| F4         | 62                  |
| F5         | 63                  |

Typical polling loop:

```basic
DIM k AS STRING
DO
    k = INKEY$
    IF k = CHR$(27) THEN EXIT DO   ' ESC to quit
    IF LEN(k) > 0 THEN
        IF ASC(k) = 0 THEN
            ' Extended key
            SELECT CASE ASC(MID$(k, 2, 1))
                CASE 72: PRINT "Up"
                CASE 80: PRINT "Down"
                CASE 75: PRINT "Left"
                CASE 77: PRINT "Right"
            END SELECT
        ELSE
            PRINT "Key: "; k
        END IF
    END IF
LOOP
```

> **Implementation note:** `INKEY$` uses raw `termios` mode on HP-UX
> (not ncurses) to avoid interfering with normal terminal output.  The
> terminal is restored to its original state when the program exits.
> If a `SHELL` command is executed while `INKEY$` has been used, the
> terminal is temporarily restored before the child process runs and
> raw mode is re-entered afterwards.

### TERMROWS / TERMCOLS

```basic
rows% = TERMROWS
cols% = TERMCOLS
```

Returns (as an `INTEGER`) the current terminal height in rows/lines (`TERMROWS`) and width in columns/characters (`TERMCOLS`), queried via `ioctl(TIOCGWINSZ)` on HP-UX (with fallback to the `LINES`/`COLUMNS` environment variables, or `24`×`80` if not running on a terminal).

```basic
PRINT "Terminal size: "; TERMCOLS; "x"; TERMROWS
```

### COMMAND$

```basic
allArgs$ = COMMAND$
arg1$    = COMMAND$(1)
prog$    = COMMAND$(0)
```

Returns command-line arguments passed to the compiled BASIC program:
- `COMMAND$` (with no arguments) returns all arguments (`argv[1]` through `argv[argc-1]`) joined by spaces, or `""` if none were provided.
- `COMMAND$(n)` returns a specific argument (`n = 1` for the first argument, `n = 2` for the second, `n = 0` for the program name `argv[0]`), or `""` if `n` is out of range.

---

## 13. File I/O

### OPEN

```basic
OPEN filename FOR mode AS #filenum
OPEN filename FOR RANDOM AS #filenum LEN = reclen
```

| Mode | Direction | Description |
|---|---|---|
| `INPUT` | Read | Sequential text input |
| `OUTPUT` | Write | Sequential text output (creates/truncates) |
| `APPEND` | Write | Sequential text output (appends to existing) |
| `RANDOM` | Read/Write | Fixed-length record access |
| `BINARY` | Read/Write | Raw byte access |

`filenum` is an integer expression in the range 1–255.

### CLOSE

```basic
CLOSE #filenum
CLOSE #filenum, #filenum2
CLOSE                             ' close all open files
```

### Sequential WRITE # / INPUT #

`WRITE #` writes values in CSV format (strings quoted, items comma-separated).
`INPUT #` reads values written by `WRITE #`.

```basic
OPEN "data.csv" FOR OUTPUT AS #1
WRITE #1, "Alice", 42, 3.14
CLOSE #1

OPEN "data.csv" FOR INPUT AS #1
DIM name AS STRING, age AS INTEGER, score AS DOUBLE
INPUT #1, name, age, score
CLOSE #1
```

### Sequential PRINT #

`PRINT #` writes undelimited text (same formatting as console `PRINT`).

```basic
OPEN "log.txt" FOR OUTPUT AS #2
PRINT #2, "Log entry: "; timestamp$
CLOSE #2
```

### Random access FIELD / LSET / RSET / GET # / PUT #

Random-access files (`OPEN ... FOR RANDOM AS #filenum [LEN = reclen]`, default `reclen = 128`) support both GW-BASIC `FIELD` buffers and direct `TYPE` record variables:

```basic
FIELD #filenum, width1 AS var1$ [, width2 AS var2$, ...]
LSET var$ = expr$        ' left-justify and space-pad into var$ (preserves LEN(var$))
RSET var$ = expr$        ' right-justify and space-pad into var$ (preserves LEN(var$))

' Binary number <-> fixed-length string conversions for FIELD buffers:
s$ = MKI$(intVal%)       ' 2-byte binary string from INTEGER
s$ = MKL$(longVal&)      ' 4-byte binary string from LONG
s$ = MKS$(sngVal!)       ' 4-byte binary string from SINGLE
s$ = MKD$(dblVal#)       ' 8-byte binary string from DOUBLE

n% = CVI(s$)             ' INTEGER from 2-byte binary string
n& = CVL(s$)             ' LONG from 4-byte binary string
n! = CVS(s$)             ' SINGLE from 4-byte binary string
n# = CVD(s$)             ' DOUBLE from 8-byte binary string

' Record number is 1-based; omit recno for sequential record access.
' Omit variable to read/write the FIELD buffer for #filenum:
GET #filenum [, recno] [, variable]
PUT #filenum [, recno] [, variable]
```

#### Example (GW-BASIC style `FIELD` + `LSET` / `RSET`)

```basic
OPEN "emp.dat" FOR RANDOM AS #1 LEN = 30
FIELD #1, 20 AS empName$, 2 AS empAge$, 8 AS empSalary$

' Write record 1
LSET empName$   = "Alice"
LSET empAge$    = MKI$(34)
LSET empSalary$ = MKD$(72500.50)
PUT #1, 1

' Read record 1 back
GET #1, 1
PRINT RTRIM$(empName$); " age="; CVI(empAge$); " salary="; CVD(empSalary$)
CLOSE #1
```

### File status functions

```basic
EOF(filenum)             ' non-zero if at end of file
LOC(filenum)             ' current record number (RANDOM) or byte position
LOF(filenum)             ' file size in bytes
```

---

## 14. Error Handling

The compiler supports two error-handling mechanisms.

### TRY / CATCH / END TRY (primary)

```basic
TRY
    statements
CATCH [varname AS STRING]
    statements
END TRY
```

If any statement in the `TRY` block raises a runtime error, execution jumps
to the `CATCH` block.  The optional `varname` variable receives the error
message string.

```basic
TRY
    DIM a(5) AS INTEGER
    PRINT a(100)              ' raises error 9: Subscript out of range
CATCH e AS STRING
    PRINT "Error: "; e
END TRY
```

TRY blocks may be nested.  The innermost enclosing TRY catches first.

### ON ERROR GOTO (legacy)

```basic
ON ERROR GOTO LabelName        ' arm the error handler
ON ERROR GOTO 0                ' disarm (errors are fatal again)
```

When an error occurs and no TRY block is active, execution jumps to
`LabelName`.  Inside the handler:

- `ERR` — error code number (INTEGER).
- `ERL` — line number where error occurred (INTEGER).
- `ERROR$(n)` — error message string for code `n`.

After handling, use `RESUME NEXT` to continue after the statement that
caused the error:

```basic
ON ERROR GOTO ErrHandler
x = 1 \ 0                     ' division by zero
PRINT "continued"
GOTO Done

ErrHandler:
    PRINT "Error "; ERR; ": "; ERROR$(ERR)
    RESUME NEXT

Done:
    PRINT "done"
```

> **Note:** Plain `RESUME` (re-execute the faulting statement) is not
> supported.  Use `RESUME NEXT` or `GOTO` within the handler.

### Runtime errors raised automatically

| Situation | Error code |
|---|---|
| Division by zero (`/` or `\`) | 11 |
| Array subscript out of range | 9 |
| `SQR` of negative number | 5 |
| `LOG` of non-positive number | 5 |
| Bad file number | 52 |
| File not open | 52 |
| Input past end of file | 62 |
| Out of memory (`DIM` failure) | 7 |

---

## 15. Built-in Functions — Strings

All string indices are **1-based**.

| Function | Returns | Description |
|---|---|---|
| `LEN(s$)` | INTEGER | Length of string in characters |
| `LEFT$(s$, n)` | STRING | First `n` characters |
| `RIGHT$(s$, n)` | STRING | Last `n` characters |
| `MID$(s$, start)` | STRING | Substring from `start` to end |
| `MID$(s$, start, len)` | STRING | Substring of length `len` starting at `start` |
| `UCASE$(s$)` | STRING | Uppercase copy |
| `LCASE$(s$)` | STRING | Lowercase copy |
| `LTRIM$(s$)` | STRING | Remove leading spaces |
| `RTRIM$(s$)` | STRING | Remove trailing spaces |
| `TRIM$(s$)` | STRING | Remove leading and trailing spaces |
| `INSTR(hay$, needle$)` | INTEGER | Position of `needle$` in `hay$`; 0 if not found |
| `INSTR(start, hay$, needle$)` | INTEGER | Search from `start` |
| `STR$(n)` | STRING | Numeric value as string (up to 15 significant digits; positive values get leading space) |
| `VAL(s$)` | DOUBLE | Parse string as number; returns 0 on failure |
| `CHR$(n)` | STRING | Character with ASCII code `n` |
| `ASC(s$)` | INTEGER | ASCII code of first character; 0 if empty |
| `SPACE$(n)` | STRING | String of `n` space characters |
| `STRING$(n, ch)` | STRING | String of `n` copies of character code `ch` (or first char of `ch$`) |
| `HEX$(n)` | STRING | Hexadecimal representation of `n` |
| `OCT$(n)` | STRING | Octal representation of `n` |

### MID$ as an assignment target

`MID$` may appear on the left-hand side of an assignment to replace
characters in-place.  The string is **not** extended.

```basic
DIM s AS STRING
s = "Hello World"
MID$(s, 7, 5) = "BASIC"    ' s is now "Hello BASIC"
MID$(s, 1, 5) = "Goodbye"  ' only first 5 chars replaced: "Goodb BASIC"
```

---

## 16. Built-in Functions — Math

All math functions accept and return `DOUBLE` unless noted.

| Function | Returns | Description |
|---|---|---|
| `SIN(x)` | DOUBLE | Sine (radians) |
| `COS(x)` | DOUBLE | Cosine (radians) |
| `TAN(x)` | DOUBLE | Tangent (radians) |
| `ATN(x)` | DOUBLE | Arc tangent (radians) |
| `SQR(x)` | DOUBLE | Square root; error 5 if `x < 0` |
| `LOG(x)` | DOUBLE | Natural logarithm; error 5 if `x <= 0` |
| `EXP(x)` | DOUBLE | `e` raised to the power `x` |
| `ABS(x)` | same as x | Absolute value |
| `INT(x)` | LONG | Floor: largest integer ≤ `x` (`INT(-3.2)` = −4) |
| `FIX(x)` | LONG | Truncate toward zero (`FIX(-3.7)` = −3) |
| `SGN(x)` | INTEGER | Sign: −1, 0, or 1 |
| `RND` | SINGLE | Random number in [0, 1) |
| `RND(n)` | SINGLE | `n > 0`: next random; `n = 0`: repeat last; `n < 0`: reseed |
| `RANDOMIZE` | — | Seed RNG with current time |
| `RANDOMIZE seed` | — | Seed RNG with `seed`; `−1` uses current time |

---

## 17. Built-in Functions — Conversion

| Function | Returns | Description |
|---|---|---|
| `CINT(x)` | INTEGER | Round to nearest integer (banker's rounding) |
| `CLNG(x)` | LONG | Round to nearest long |
| `CSNG(x)` | SINGLE | Cast to single-precision float |
| `CDBL(x)` | DOUBLE | Cast to double-precision float |
| `MKI$(n%)` | STRING | Pack 16-bit integer into a 2-byte binary string |
| `MKL$(n&)` | STRING | Pack 32-bit long integer into a 4-byte binary string |
| `MKS$(x!)` | STRING | Pack single-precision float into a 4-byte binary string |
| `MKD$(x#)` | STRING | Pack double-precision float into an 8-byte binary string |
| `CVI(s$)` | INTEGER | Unpack 2-byte binary string into an INTEGER |
| `CVL(s$)` | LONG | Unpack 4-byte binary string into a LONG |
| `CVS(s$)` | SINGLE | Unpack 4-byte binary string into a SINGLE |
| `CVD(s$)` | DOUBLE | Unpack 8-byte binary string into a DOUBLE |

---

## 18. Built-in Functions — File Status

| Function | Returns | Description |
|---|---|---|
| `EOF(n)` | INTEGER | `−1` (true) if file `n` is at end-of-file, `0` (false) otherwise |
| `LOC(n)` | LONG | Current position: record number (RANDOM) or byte offset |
| `LOF(n)` | LONG | File size in bytes |

---

## 19. Runtime Error Codes

These are the values returned by `ERR` and accepted by `ERROR$()`.

| Code | Message |
|---|---|
| 1 | NEXT without FOR |
| 2 | Syntax error |
| 3 | RETURN without GOSUB |
| 4 | Out of DATA |
| 5 | Illegal function call |
| 6 | Overflow |
| 7 | Out of memory |
| 9 | Subscript out of range |
| 10 | Duplicate definition |
| 11 | Division by zero |
| 13 | Type mismatch |
| 14 | Out of string space |
| 19 | No RESUME |
| 20 | RESUME without error |
| 24 | Device timeout |
| 27 | Device I/O error |
| 50 | Field overflow |
| 52 | Bad file number |
| 53 | File not found |
| 54 | Bad file mode |
| 55 | File already open |
| 57 | Device I/O error |
| 58 | File already exists |
| 61 | Disk full |
| 62 | Input past end of file |
| 63 | Bad record number |
| 64 | Bad file name |
| 67 | Too many files |

Any other code is reported as `"Error N"`.

---

## 20. Compiler Internals

This section is intended for developers modifying or extending the compiler.

### Pipeline

```
.bas source
    │
    ▼
Lexer (compiler/lexer.c)
    │   Produces: token stream (Token structs)
    ▼
Parser (compiler/parser.c)
    │   Produces: AST rooted at ND_BLOCK
    ▼
Type System (compiler/typesys.c)
    │   Annotates: resolved_type on every expression node
    │   Populates: symbol table (compiler/symtab.c)
    ▼
Emitter (compiler/emitter.c)
    │   Produces: .c source file
    ▼
gcc 4.2 (invoked by compiler/main.c)
    │   Links: libbasic.a, libm
    ▼
PA-RISC 32-bit ELF binary
```

### Source file map

| File | Role |
|---|---|
| `compiler/main.c` | Entry point, argument parsing, pipeline orchestration, gcc invocation |
| `compiler/token.h` | `TokenType` enum, `Token` struct |
| `compiler/lexer.h` / `lexer.c` | Tokenizer; 97-keyword binary-search table |
| `compiler/ast.h` | `NodeKind` enum, `AstNode` tagged union, `ParamDef`, `DimSpec` |
| `compiler/ast.c` | `make_node()`, `free_ast()`, `print_ast()` |
| `compiler/parser.h` / `parser.c` | Recursive-descent parser; ~2300 lines |
| `compiler/symtab.h` / `symtab.c` | Scoped hash-table symbol table |
| `compiler/typesys.h` / `typesys.c` | Semantic analysis, type inference, built-in table |
| `compiler/emitter.h` / `emitter.c` | AST → C transpiler; ~2300 lines |
| `compiler/error.h` / `error.c` | Compiler error/warning reporting |
| `runtime/basic_runtime.h` | Public API for all runtime modules |
| `runtime/basic_runtime.c` | `bs_runtime_init()`, `bs_runtime_cleanup()` |
| `runtime/basic_string.c` | `BString` type and all string built-ins |
| `runtime/basic_io.c` | PRINT, PRINT USING, INPUT, LINE INPUT |
| `runtime/basic_file.c` | File I/O; 256-entry file-number table |
| `runtime/basic_math.c` | Math built-in wrappers over `libm` |
| `runtime/basic_error.c` | TRY/CATCH frame stack; ON ERROR; error code table |
| `runtime/basic_array.c` | `BArray` heap array; REDIM PRESERVE |
| `runtime/basic_lprint.c` | `LPRINT` / `LPRINT USING` spooler integration (`lp`) |
| `runtime/basic_shell.c` | `SHELL`, `SHELL$`, `INKEY$`, `ERRLEVEL`, `TERMROWS`, `TERMCOLS` |

### AST node kinds

**Statement nodes:**

`ND_BLOCK`, `ND_ASSIGN`, `ND_IF`, `ND_FOR`, `ND_WHILE`, `ND_DO`,
`ND_SELECT`, `ND_GOSUB`, `ND_GOTO`, `ND_RETURN`, `ND_SUB_DEF`,
`ND_FUNC_DEF`, `ND_CALL`, `ND_DIM`, `ND_REDIM`, `ND_TYPE_DEF`,
`ND_PRINT`, `ND_LPRINT`, `ND_LPRINT_OPEN`, `ND_INPUT`, `ND_LINE_INPUT`,
`ND_OPEN`, `ND_CLOSE`, `ND_READ`, `ND_WRITE`, `ND_GET`, `ND_PUT`,
`ND_FIELD`, `ND_LSET`, `ND_RSET`, `ND_TRY`, `ND_CATCH`,
`ND_ON_ERROR`, `ND_RESUME_NEXT`, `ND_EXIT_SUB`, `ND_EXIT_FUNC`,
`ND_EXIT_FOR`, `ND_EXIT_DO`, `ND_SHELL`, `ND_CLS`, `ND_LOCATE`,
`ND_COLOR`, `ND_BGCOLOR`, `ND_END`, `ND_LABEL`

**Expression nodes:**

`ND_BINOP`, `ND_UNOP`, `ND_INT_LIT`, `ND_LONG_LIT`, `ND_FLOAT_LIT`,
`ND_DOUBLE_LIT`, `ND_STRING_LIT`, `ND_VAR`, `ND_ARRAY_INDEX`,
`ND_FIELD_ACCESS`, `ND_FUNC_CALL`, `ND_CAST`, `ND_MID_ASSIGN`

### Emitted C conventions

| BASIC construct | Emitted C |
|---|---|
| `INTEGER` variable `x%` | `int x` |
| `STRING` variable `s$` | `BString s` |
| `SUB Foo(a AS INTEGER)` | `static void sub_foo(int a)` |
| `FUNCTION Bar() AS DOUBLE` | `static double func_bar(void)` |
| `TYPE Point` | `typedef struct { ... } TYPE_point, TYPE_0` |
| Label `Loop:` | `label_loop: ;` |
| `GOTO Loop` | `goto label_loop;` |
| `TRY` block | `BsErrorFrame f; bs_error_push(&f); if(setjmp(f.jmp)==0){...}else{...}` |
| String concatenation | `__bas_strcat_tmp(uid, &a, &b)` |
| Array index | `*(int*)__bas_arr_idx(&arr, ndims, i, j)` |

### String temporary pool

String expression temporaries use a static ring-buffer of 256 `BString` slots
(`__bas_strpool[]`).  Each temporary function (`__bas_strlit`, `__bas_strcat_tmp`,
etc.) takes a `uid` argument; `uid % 256` selects the slot.  The pool is not
thread-safe, which is intentional for single-threaded BASIC programs.

### C identifier mangling

- Sigils are stripped: `a$` → `a`, `count%` → `count`.
- All names are lowercased.
- Names that collide with C keywords get a `_bas_` prefix:
  `int` → `_bas_int`, `long` → `_bas_long`, etc.
- SUB `Foo` → `sub_foo`; FUNCTION `Bar` → `func_bar`; TYPE `Point` → `TYPE_Point`.
- Labels: `Loop` → `label_loop`.
- Compiler-generated temporaries use `__bas_` prefix.

---

## 21. Limitations and Known Differences

### Compared to QuickBASIC / FreeBASIC

| Feature | Status |
|---|---|
| Line numbers | Not supported (not required; use labels) |
| `RESUME` (re-execute faulting statement) | **Not supported** — use `RESUME NEXT` |
| `DATA` / `READ` (data embedded in source) | Not supported |
| `CHAIN` (run another BASIC program) | Not supported |
| `PEEK` / `POKE` (raw memory access) | Not supported |
| `COMMON` blocks | Not supported |
| Multiple source file modules | Not supported (single file per compilation) |
| Inline assembly | Not supported |
| Object-oriented extensions | Not supported |
| `PRINT USING` / `LPRINT USING` `_` literal escape | Not supported (`_` is printed literally) |
| `PRINT USING` / `LPRINT USING` `**$` combined prefix | Not supported (use `$**` or `$$` / `**` separately) |
| `PRINT USING` / `LPRINT USING` `%` overflow prefix | Not supported (numbers wider than `#` mask print in full without `%`) |
| `PRINT USING` / `LPRINT USING` format cycling | Not supported (one pass per statement; extra args beyond format fields are ignored) |

### PA-RISC 1.1 / HP-UX 11i V1 specific

- `int` and `long` are both 32 bits; `LONG` offers no range advantage
  over `INTEGER` on this platform.
- 64-bit integers (`LONGLONG` or equivalent) are not available in the default
  PA-RISC 1.1 32-bit ABI without `-mlp64`.
- `double` values must be 8-byte aligned; the compiler ensures this via
  standard C struct layout rules.
- The native HP-UX `make` does not support GNU make extensions (`ifeq`,
  `-include`); always use `gmake` to build the compiler.
- The HP-UX `/usr/bin/install` utility does not support the `-d` flag;
  the `Makefile` uses `mkdir -p` / `cp` / `chmod` instead.

### Compiler error recovery

The compiler attempts to continue parsing after a syntax error (skips to the
next newline) to report multiple errors in one pass.  However, cascading
errors from a single root cause are common.  Fix the first reported error
first.

### Implicit variable declaration

Variables used without `DIM` are implicitly declared as `SINGLE` (or the
type implied by their sigil) with a warning.  This matches QuickBASIC
behaviour but can mask typos.  Use `DIM` for all variables in production code.
