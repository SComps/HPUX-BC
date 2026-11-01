# HPUX-BC (`bcc`)

An ANSI C89 QBasic / QuickBASIC 4.5–compatible BASIC-to-C compiler (`bcc`), runtime library (`libbasic.a`), and full-screen VT100/ANSI terminal IDE (`bcc-ide`), originally built for **HP-UX 11i on PA-RISC** and portable across **POSIX / Linux (32-bit & 64-bit)** systems.

## Features

- **QBasic / QuickBASIC 4.5 Compatible Dialect**
  - Structured control flow (`SUB`, `FUNCTION`, `IF/ELSEIF/ELSE`, `SELECT CASE`, `DO/LOOP`, `FOR/NEXT`, `WHILE/WEND`, `EXIT`)
  - User-defined `TYPE ... END TYPE` records, fixed & dynamic arrays (`DIM`, `REDIM`, `REDIM PRESERVE`, `ERASE`)
  - `TRY / CATCH / END TRY` structured error handling + classic `ON ERROR GOTO`
  - Sequential (`INPUT`, `OUTPUT`, `APPEND`), `RANDOM` (`FIELD`, `GET`, `PUT`, `LSET`, `RSET`, `MKI$`/`CVI`), and `BINARY` file I/O
  - Formatted output with `PRINT USING` and `LPRINT USING` (`lp` / `lpr` spooler integration)
  - Full VT100/ANSI terminal screen control (`CLS`, `LOCATE`, `COLOR`, `BGCOLOR`, `INKEY$`, `TERMCOLS`, `TERMROWS`)
- **Zero External Dependencies**
  - Pure ANSI C89 compiler and runtime (`-lm` only; no `ncurses` required)
- **Self-Hosted Terminal IDE (`bcc-ide`)**
  - Written in BASIC and compiled by `bcc` itself

## Building & Installing

Use GNU Make (`gmake` on HP-UX, `make` on Linux):

```sh
./configure
gmake
gmake check
gmake install
```

To build and install the terminal IDE (`bcc-ide`):

```sh
cd ide
gmake
gmake install
```

## Usage

```sh
# Compile a BASIC program to a native executable
bcc hello.bas -o hello
./hello

# Open a file in the terminal IDE
bcc-ide hello.bas
```

## Documentation

See [**REFERENCE.md**](REFERENCE.md) for the complete language reference, built-in function catalog, compiler flags, and GW-BASIC / QuickBASIC 4.5 compatibility notes.
