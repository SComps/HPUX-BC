# HPUX-BC (`bcc`)

An ANSI C89 QBasic / QuickBASIC 4.5–compatible BASIC-to-C compiler (`bcc`), runtime library (`libbasic.a` / `basic.lib`), and full-screen VT100/ANSI terminal IDE (`bcc-ide`), originally built for **HP-UX 11i on PA-RISC** and portable across **POSIX / Linux (32-bit & 64-bit)** and **Windows (Visual Studio)**.

## Features

- **QBasic / QuickBASIC 4.5 Compatible Dialect**
  - Structured control flow (`SUB`, `FUNCTION`, `IF/ELSEIF/ELSE`, `SELECT CASE`, `DO/LOOP`, `FOR/NEXT`, `WHILE/WEND`, `EXIT`)
  - User-defined `TYPE ... END TYPE` records, fixed & dynamic arrays (`DIM`, `REDIM`, `REDIM PRESERVE`, `ERASE`)
  - `TRY / CATCH / END TRY` structured error handling + classic `ON ERROR GOTO`
  - Sequential (`INPUT`, `OUTPUT`, `APPEND`), `RANDOM` (`FIELD`, `GET`, `PUT`, `LSET`, `RSET`, `MKI$`/`CVI`), and `BINARY` file I/O
  - Formatted output with `PRINT USING` and `LPRINT USING` (`lp` / `lpr` spooler integration)
  - Full VT100/ANSI terminal screen control (`CLS`, `LOCATE`, `COLOR`, `BGCOLOR`, `INKEY$`, `TERMCOLS`, `TERMROWS`)
- **Zero External Dependencies**
  - Pure ANSI C89 compiler and runtime (`-lm` on Unix; standard Win32 Console APIs on Windows; no `ncurses` required)
- **Self-Hosted Terminal IDE (`bcc-ide`)**
  - Written in BASIC and compiled by `bcc` itself

## Building & Installing

### HP-UX 11i & Linux / POSIX

Use GNU Make (`gmake` on HP-UX, `make` on Linux):

```sh
./configure --prefix=/your/path
gmake
gmake check
gmake install
```

your path might be /usr/local

To build and install the terminal IDE (`bcc-ide`):

```sh
cd ide
gmake
gmake install
```

### Windows (Visual Studio / NMAKE)

Open a **x64 Native Tools Command Prompt** (or **Developer PowerShell**) for Visual Studio (e.g., Visual Studio 2022 / 2026) and run:

```bat
REM Build bcc.exe and basic.lib
nmake /f Makefile.win

REM Also build the terminal IDE (ide\bcc-ide.exe)
nmake /f Makefile.win ide

REM Optional: install bcc.exe, basic.lib, basic_runtime.h, and bcc-ide.exe (default: C:\hpux-bc)
nmake /f Makefile.win install PREFIX=C:\hpux-bc

REM Clean build artifacts
nmake /f Makefile.win clean
```

> **Note:** On Windows, `bcc.exe` automatically locates its runtime files (`basic.lib` and `basic_runtime.h`) relative to `bcc.exe`'s own directory (`GetModuleFileNameA`), and automatically initializes Visual Studio's `vcvars64.bat` if invoked from a standard Command Prompt or PowerShell window.

## Usage

```sh
# Compile a BASIC program to a native executable
bcc hello.bas -o hello
./hello

# Open a file in the terminal IDE
bcc-ide hello.bas
```

## Documentation

- [**Introduction to BASIC (bcc).pdf**](<Introduction to BASIC (bcc).pdf>) — New User Guide & Tutorial
- [**REFERENCE.md**](REFERENCE.md) — Complete Language Reference, Built-in Function Catalog, Compiler Flags, and GW-BASIC / QuickBASIC 4.5 Compatibility Notes

