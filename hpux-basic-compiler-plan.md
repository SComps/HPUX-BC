# HP-UX BASIC Compiler — Project Plan

## Top-Level Overview

Build a **QuickBASIC/FreeBASIC-style structured BASIC compiler** written in C,
targeting HP-UX 11i V1 (11.11) on PA-RISC 1.1 (32-bit, big-endian).

**Approach:** The compiler is a **source-to-source transpiler** — it reads `.bas`
source files and emits `.c` files, which are then compiled by `gcc 4.2` on HP-UX.
A companion static library `libbasic.a` provides the BASIC runtime (strings, I/O,
math, error handling).

**Toolchain on target (HP-UX 11i V1 PA1.1):**
- `gcc 4.2` — C compiler and final code generator
- `gmake` — build system
- `gmp`, `db`, `ncurses` — available but only pulled in if needed
- No `flex`/`bison` — parser is hand-written recursive-descent in C

**Compiler is itself written in C** compilable by `gcc 4.2` on HP-UX. It must
be fully self-hosted — no host-side tooling required beyond `gcc` and `gmake`.

**PA-RISC 1.1 ABI constraints (apply to emitted C, not the compiler itself):**
- `int` = 32-bit, `long` = 32-bit, `pointer` = 32-bit
- Big-endian
- Default stack alignment is 64-bit (doubles must be 8-byte aligned)
- No 64-bit integer type in the default PA1.1 ABI under gcc 4.2 without `-mlp64`

---

## Language Feature Scope

| Feature | In Scope |
|---|---|
| Variables: INTEGER, LONG, SINGLE, DOUBLE, STRING | Yes |
| Type suffix sigils: `%` `&` `!` `#` `$` | Yes |
| Expressions: arithmetic, relational, logical, bitwise | Yes |
| IF / THEN / ELSE / ELSEIF / END IF | Yes |
| FOR / NEXT (STEP, named-variable close) | Yes |
| WHILE / WEND | Yes |
| DO / LOOP (WHILE, UNTIL, top and bottom) | Yes |
| SELECT CASE / CASE / CASE ELSE / END SELECT | Yes |
| GOSUB / RETURN (line-label, not line-number) | Yes |
| GOTO (label) | Yes |
| SUB / END SUB, FUNCTION / END FUNCTION | Yes |
| BYVAL / BYREF parameters | Yes |
| LOCAL variables, module-level globals | Yes |
| DIM (static and dynamic with heap allocation) | Yes |
| REDIM (resize heap arrays, optionally PRESERVE) | Yes |
| Multi-dimensional arrays (up to 8 dimensions) | Yes |
| TYPE / END TYPE (user-defined structs) | Yes |
| PRINT, PRINT USING | Yes |
| INPUT, LINE INPUT | Yes |
| OPEN / CLOSE / READ / WRITE / GET / PUT | Yes |
| EOF, LOC, LOF | Yes |
| TRY / CATCH / END TRY (primary error handling) | Yes |
| ON ERROR GOTO, RESUME NEXT, ERR, ERL, ERROR$ (legacy compat) | Yes |
| RESUME (re-execute faulting statement — requires per-statement labels) | No |
| Built-in math functions (SIN, COS, SQR, LOG, EXP, etc.) | Yes |
| Built-in string functions (LEN, MID$, LEFT$, RIGHT$, etc.) | Yes |
| Built-in conversion functions (STR$, VAL, CHR$, ASC, etc.) | Yes |
| CHAIN (include/run another .bas module) | No |
| PEEK / POKE (raw memory access) | No |
| Inline assembly | No |
| Object-oriented features | No |
| Namespaces / modules | No |
| Preprocessor / macros | No |

---

## Repository Layout

```
hpux-bc/
  Makefile              — builds compiler (bcc) and runtime (libbasic.a)
  compiler/
    main.c              — entry point, argument parsing, pipeline driver
    lexer.h / lexer.c   — tokenizer
    token.h             — token type enum and Token struct
    parser.h / parser.c — recursive-descent parser → AST
    ast.h               — AST node types and structs
    symtab.h / symtab.c — symbol table (scoped, hash-based)
    typesys.h / typesys.c — type system and type-checking
    emitter.h / emitter.c — C code emitter (AST → .c text)
    error.h / error.c   — compiler error/warning reporting
  runtime/
    basic_runtime.h     — public API included by all emitted C files
    basic_string.c      — dynamic string type (BString)
    basic_io.c          — PRINT, INPUT, LINE INPUT
    basic_file.c        — OPEN/CLOSE/READ/WRITE/GET/PUT/EOF/LOC/LOF
    basic_math.c        — math built-ins wrapping libm
    basic_error.c       — TRY/CATCH, ON ERROR / RESUME NEXT / ERR / ERL / ERROR$
    basic_array.c       — DIM/REDIM heap array descriptor and bounds checking
  tests/
    *.bas               — test programs exercising each feature area
    run_tests.sh        — simple shell script test runner
```

---

## Sub-Tasks

---

### Sub-Task 1 — Project Scaffold and Build System

**Status:** `[x] done`

**Intent:**
Establish the directory layout, Makefile, and skeleton C files so the project
builds cleanly from the start. Every subsequent sub-task adds to this skeleton.

**Expected Outcomes:**
- `gmake` produces `bcc` (the compiler binary) with no warnings
- `gmake` produces `libbasic.a` with no warnings
- Both targets are empty stubs but link cleanly

**Todo List:**
1. Create the directory tree: `compiler/`, `runtime/`, `tests/`
2. Write `Makefile` with targets: `all`, `bcc`, `libbasic.a`, `clean`, `test`
3. Write stub `compiler/main.c` — opens input file, prints usage, exits
4. Write stub header files: `token.h`, `ast.h`, `lexer.h`, `parser.h`,
   `symtab.h`, `typesys.h`, `emitter.h`, `error.h`
5. Write empty `.c` bodies for each compiler module
6. Write stub `runtime/basic_runtime.h` and empty `.c` bodies
7. Verify `gmake` and `gmake clean` succeed

**Relevant Context:**
- `gcc 4.2` flags to use: `-Wall -Wextra -pedantic -std=c89` (c89 for maximum
  HP-UX portability; avoid C99 VLAs)
- `gmake` is the make variant on this system
- `libbasic.a` is built with `ar` + `ranlib`
- PA-RISC 1.1: pass `-march=1.1` to gcc for the emitted C compilation, not
  for the compiler itself (use plain `-O2` for the compiler build)

---

### Sub-Task 2 — Lexer / Tokenizer

**Status:** `[x] done`

**Intent:**
Implement the complete tokenizer that converts raw BASIC source text into a
flat stream of typed tokens. This is the foundation everything else sits on.

**Expected Outcomes:**
- `lexer.c` correctly tokenizes all keywords, identifiers, sigils, literals,
  operators, and punctuation
- Line and column tracking works (needed for error messages)
- A simple standalone test (driven from `main.c` with a `--lex` flag) prints
  the token stream for a given `.bas` file

**Todo List:**
1. Define `TokenType` enum in `token.h` covering:
   - All BASIC keywords (IF, THEN, ELSE, FOR, NEXT, WHILE, WEND, DO, LOOP,
     SELECT, CASE, SUB, FUNCTION, DIM, REDIM, TYPE, END, GOSUB, GOTO, RETURN,
     ON, ERROR, RESUME, OPEN, CLOSE, READ, WRITE, GET, PUT, PRINT, INPUT,
     LINE, BYVAL, BYREF, AS, TO, STEP, MOD, AND, OR, NOT, XOR, etc.)
   - Literals: TOK_INT_LIT, TOK_LONG_LIT, TOK_FLOAT_LIT, TOK_DOUBLE_LIT,
     TOK_STRING_LIT
   - Sigils: TOK_SIGIL_INT(`%`), TOK_SIGIL_LONG(`&`), TOK_SIGIL_SINGLE(`!`),
     TOK_SIGIL_DOUBLE(`#`), TOK_SIGIL_STRING(`$`)
   - Operators: `+` `-` `*` `/` `\` (integer divide) `^` `=` `<>` `<` `>`
     `<=` `>=` `&` (string concat)
   - Punctuation: `(` `)` `,` `;` `:` `\n` (significant — BASIC is
     line-oriented)
   - TOK_IDENT, TOK_LABEL (identifier followed by `:`), TOK_EOF
2. Define `Token` struct: `{ TokenType type; char *text; int line; int col; }`
3. Implement `lexer.c`: character-by-character scan, keyword lookup table
   (binary search or perfect hash), string literal escape handling
4. Handle line continuation (`_` at end of line)
5. Handle REM and `'` comments (consume to end of line)
6. Handle case-insensitivity (fold to uppercase during scan)
7. Add `--lex` debug mode to `main.c`

**Relevant Context:**
- BASIC is **line-oriented**: newline is a real token (statement separator)
- Colons `:` are also statement separators (multiple statements per line)
- Identifiers followed immediately by a sigil (`A$`, `X%`) — sigil is part of
  the name for lookup purposes
- String literals use `"` delimiters; `""` is an escaped quote inside a string
- Integer literals ending in `&` are LONG type (e.g. `32768&`)

---

### Sub-Task 3 — AST Node Definitions

**Status:** `[x] done`

**Intent:**
Define the complete set of Abstract Syntax Tree node types in `ast.h`. The AST
is the data contract between the parser and all downstream phases. Getting this
right before writing the parser avoids costly rework.

**Expected Outcomes:**
- `ast.h` defines every node type needed to represent the full language scope
- A `free_ast()` function in `ast.c` correctly deallocates any tree
- No circular dependencies between headers

**Todo List:**
1. Define `NodeKind` enum covering:
   - Statements: ND_ASSIGN, ND_IF, ND_FOR, ND_WHILE, ND_DO, ND_SELECT,
     ND_GOSUB, ND_GOTO, ND_RETURN, ND_SUB_DEF, ND_FUNC_DEF, ND_CALL,
     ND_DIM, ND_REDIM, ND_TYPE_DEF, ND_PRINT, ND_INPUT, ND_LINE_INPUT,
     ND_OPEN, ND_CLOSE, ND_READ, ND_WRITE, ND_GET, ND_PUT,
     ND_TRY, ND_CATCH, ND_ON_ERROR, ND_RESUME_NEXT, ND_EXIT_SUB, ND_EXIT_FOR, ND_EXIT_DO,
     ND_END, ND_LABEL, ND_BLOCK
   - Expressions: ND_BINOP, ND_UNOP, ND_INT_LIT, ND_LONG_LIT,
     ND_FLOAT_LIT, ND_DOUBLE_LIT, ND_STRING_LIT, ND_VAR, ND_ARRAY_INDEX,
     ND_FIELD_ACCESS, ND_FUNC_CALL, ND_CAST
2. Define `AstNode` as a tagged union struct; keep it flat (no vtable)
3. Define `ParamDef` struct for SUB/FUNCTION parameter lists
4. Define `DimSpec` struct for array dimension bounds
5. Write `ast.c` with `make_node()` allocator and `free_ast()` walker
6. Use only `malloc`/`free` — no arena allocator needed at this scale

**Relevant Context:**
- Keep the struct simple and C89-compatible (no `//` comments, no designated
  initialisers in definitions shared with the emitter)
- `ND_BLOCK` is a linked-list of statements (use `next` pointer on AstNode)

---

### Sub-Task 4 — Recursive-Descent Parser

**Status:** `[x] done`

**Intent:**
Implement the full parser that consumes the token stream from the lexer and
produces an AST. Hand-written recursive descent is the right choice: no
external tools, easy to produce good error messages, natural fit for BASIC.

**Expected Outcomes:**
- Parser correctly handles all in-scope language features
- Syntax errors report file, line, and column with a meaningful message
- A `--parse` debug flag in `main.c` pretty-prints the AST

**Todo List:**
1. Implement parser infrastructure: `peek()`, `consume()`, `expect()`,
   `match()` helpers operating on the lexer's token stream
2. Implement top-level: `parse_program()` — loop over statements/definitions
3. Implement statement parsers (one function each):
   - `parse_if()`, `parse_for()`, `parse_while()`, `parse_do()`
   - `parse_select()` — including range (`CASE 1 TO 5`) and list cases
   - `parse_sub_def()`, `parse_function_def()` — with parameter lists
   - `parse_dim()`, `parse_redim()` — with optional bounds expressions
   - `parse_type_def()` — field list
   - `parse_print()` — handle `;` (no newline), `,` (tab), PRINT USING
   - `parse_input()`, `parse_line_input()`
   - `parse_open()`, `parse_close()`, `parse_read()`, `parse_write()`,
     `parse_get()`, `parse_put()`
   - `parse_try()` — TRY block, CATCH clause, END TRY
   - `parse_on_error()` — ON ERROR GOTO label / ON ERROR GOTO 0
   - `parse_resume_next()` — RESUME NEXT only (plain RESUME is not supported)
   - `parse_gosub()`, `parse_goto()`, `parse_return()`
   - `parse_assign()` — LHS can be variable, array element, or TYPE field
4. Implement expression parser with correct precedence (Pratt or recursive
   descent with precedence levels):
   - Precedence (low→high): OR, AND, NOT, comparisons, `&` concat,
     `+`/`-`, `*`/`/`/`\`/MOD, unary `-`/`+`, `^`, postfix call/index
5. Handle label definitions (`LabelName:` at start of line)
6. Add `--parse` AST dump to `main.c`

**Relevant Context:**
- BASIC expression precedence differs from C in key ways: `NOT` is lower
  than comparisons; `^` (exponentiation) is right-associative and higher
  than unary minus
- `END` alone is a program-terminator statement; `END IF`, `END SUB`,
  `END FUNCTION`, `END SELECT`, `END TYPE` are block-closers — the parser
  must disambiguate by looking at the next token after `END`
- `FUNCTION` names that are also the return variable (QuickBASIC style):
  assignment to the function name inside its body sets the return value

---

### Sub-Task 5 — Symbol Table and Type System

**Status:** `[x] done`

**Intent:**
Build the scoped symbol table and type-checking pass. This pass annotates the
AST with resolved types and catches semantic errors (undeclared variables,
type mismatches, wrong argument counts, etc.).

**Expected Outcomes:**
- All variables, SUBs, FUNCTIONs, TYPE fields are tracked in the symbol table
- Each expression node is annotated with a resolved `BasicType`
- Type mismatches and undeclared-variable errors are reported with location
- BYREF parameter aliasing is recorded for use by the emitter

**Todo List:**
1. Define `BasicType` enum: BT_INTEGER, BT_LONG, BT_SINGLE, BT_DOUBLE,
   BT_STRING, BT_USER (index into type table), BT_VOID (SUB return)
2. Define `Symbol` struct: name, kind (variable/array/sub/function/type),
   BasicType, scope level, BYREF flag, array dimension count
3. Implement `symtab.c`: hash table of `Symbol*`, push/pop scope,
   lookup (walks scope chain), insert
4. Implement `typesys.c`: AST walker that:
   - Declares SUB/FUNCTION signatures in the global scope (two-pass for
     forward references)
   - Resolves variable declarations (DIM, implicit declaration via sigil)
   - Infers and checks expression types (annotate each ND_BINOP, etc.)
   - Resolves function call return types
   - Enforces BYREF: actual argument must be an lvalue
5. Handle IMPLICIT typing rules: sigil on identifier → type; no sigil → SINGLE
   by default (QuickBASIC default)
6. Handle TYPE field access: `rec.field` — look up `rec`'s user type, then
   look up `field` within it

**Relevant Context:**
- `symtab.h` and `typesys.h` are both used by the emitter, so keep their
  public interfaces minimal and stable
- Forward references to SUBs/FUNCTIONs are common; handle with a
  pre-scan pass that registers all SUB/FUNCTION names before the main walk

---

### Sub-Task 6 — C Code Emitter

**Status:** `[x] done`

**Intent:**
Implement the emitter that walks the type-annotated AST and writes a valid,
compilable C source file. The emitted C must compile cleanly under
`gcc 4.2 -Wall` on HP-UX 11i V1 PA1.1.

**Expected Outcomes:**
- For any valid `.bas` input, the emitter produces a `.c` file that compiles
  with `gcc 4.2 -Wall -o prog prog.c -lbasic -lm`
- Emitted C is readable (indented, named variables match BASIC names where
  legal in C)
- All in-scope BASIC features produce correct emitted C

**Todo List:**
1. Implement `emitter.c` infrastructure: output FILE*, indent level,
   `emit()` / `emit_line()` / `indent()` helpers
2. Emit file prologue: `#include "basic_runtime.h"`, forward declarations
   for all SUBs and FUNCTIONs
3. Emit TYPE definitions as C `struct` types
4. Emit global variable declarations
5. Emit SUB as `void sub_NAME(params)`, FUNCTION as `BasicType func_NAME(params)`
6. Emit each statement kind:
   - IF/ELSE → C `if/else if/else`
   - FOR/NEXT → C `for` loop with STEP support (positive and negative)
   - WHILE/WEND, DO/LOOP → C `while` / `do-while`
   - SELECT CASE → C `if/else if` chain (switch not used due to string and
     range cases)
   - GOSUB/RETURN → emit as inline C block with a `goto` to a label and a
     matching `return_LabelName:` label (classic GOSUB simulation)
   - GOTO → C `goto label_NAME`
   - PRINT → calls to `bs_print_*()` runtime functions
   - INPUT / LINE INPUT → calls to `bs_input_*()` runtime functions
   - OPEN/CLOSE/READ/WRITE/GET/PUT → calls to `bsfile_*()` runtime functions
   - DIM (static) → C local/global array declaration
   - DIM (dynamic) / REDIM → calls to `bsarray_alloc()` / `bsarray_realloc()`
   - TRY/CATCH → `if (setjmp(frame.jmp) == 0) { ... } else { /* CATCH */ }`
     using a stack of `BsErrorFrame` structs (see runtime/basic_error.c)
   - ON ERROR GOTO → arms the module-level error handler via `bs_on_error_goto()`;
     RESUME NEXT → `longjmp` to the frame's resume-next point (no per-statement
     labeling needed since plain RESUME is dropped)
   - EXIT SUB / EXIT FOR / EXIT DO → C `return` / `break`
7. Emit expressions: map BASIC operators to C, wrap string ops in runtime calls,
   emit type casts where needed
8. Emit BYREF parameters as C pointer parameters; emit BYREF argument as `&expr`
9. Emit file epilogue: `main()` that calls the top-level module body

**Relevant Context:**
- BASIC strings are `BString*` in the runtime — never emit naked `char*`
- `^` (exponentiation) emits as `pow(a, b)` cast to the appropriate type;
  requires `#include <math.h>` and `-lm`
- Integer division `\` in BASIC truncates toward zero — emit as C integer `/`
  after casting operands
- `TRY/CATCH` maps cleanly to a stack-allocated `BsErrorFrame` with `setjmp`;
  the emitter pushes a frame on entry and pops it on exit — no per-statement
  labeling required
- `ON ERROR GOTO` arms a module-level fallback handler; `RESUME NEXT` re-enters
  via `longjmp` to a single resume point placed after the protected statement
  block — still no per-statement labeling needed since plain RESUME is dropped

---

### Sub-Task 7 — Runtime Library: Strings

**Status:** `[x] done`

**Intent:**
Implement the `BString` dynamic string type and all string built-in functions.
This is the most complex part of the runtime because BASIC strings have
value-semantics and dynamic length — nothing like C strings.

**Expected Outcomes:**
- `BString` can be created, assigned, concatenated, and freed correctly
- All in-scope string built-ins work correctly and pass their tests
- No memory leaks under valgrind (or equivalent HP-UX tool)

**Todo List:**
1. Define `BString` struct in `basic_runtime.h`:
   `{ char *data; int len; int cap; }`
2. Implement in `basic_string.c`:
   - `bs_new(const char *s)` — create from C string
   - `bs_free(BString *b)` — release
   - `bs_assign(BString *dst, const BString *src)` — value copy
   - `bs_concat(BString *dst, const BString *a, const BString *b)`
   - `bs_len(const BString *b)` → INTEGER
   - `bs_mid(BString *dst, const BString *s, int start, int len)`
   - `bs_left(BString *dst, const BString *s, int n)`
   - `bs_right(BString *dst, const BString *s, int n)`
   - `bs_ucase(BString *dst, const BString *s)`
   - `bs_lcase(BString *dst, const BString *s)`
   - `bs_ltrim(BString *dst, const BString *s)`
   - `bs_rtrim(BString *dst, const BString *s)`
   - `bs_trim(BString *dst, const BString *s)` (TRIM$)
   - `bs_instr(int start, const BString *hay, const BString *needle)` → INTEGER
   - `bs_str(BString *dst, double val)` — STR$
   - `bs_val(const BString *s)` → DOUBLE (VAL)
   - `bs_chr(BString *dst, int code)` — CHR$
   - `bs_asc(const BString *s)` → INTEGER (ASC)
   - `bs_space(BString *dst, int n)` — SPACE$
   - `bs_string(BString *dst, int n, int ch)` — STRING$
   - `bs_hex(BString *dst, long val)` — HEX$
   - `bs_oct(BString *dst, long val)` — OCT$
   - `bs_compare(const BString *a, const BString *b)` → int (for `=`, `<>`, etc.)
3. Implement PRINT USING format string parser in `basic_io.c` (uses BString)
4. Write test `.bas` programs exercising each function

**Relevant Context:**
- Indices in BASIC strings are **1-based** — all runtime functions must
  translate to 0-based C internally
- MID$ on the left side of an assignment (`MID$(a$, 2, 3) = "XYZ"`) is a
  special statement form — the emitter emits a call to `bs_mid_assign()`
- `BString` objects on the stack need careful lifetime management —
  the emitter must emit `bs_free()` at scope exit for local strings

---

### Sub-Task 8 — Runtime Library: I/O (PRINT, INPUT, File)

**Status:** `[x] done`

**Intent:**
Implement console I/O (`PRINT`, `INPUT`, `LINE INPUT`) and file I/O
(`OPEN`/`CLOSE`/`READ`/`WRITE`/`GET`/`PUT`/`EOF`/`LOC`/`LOF`) in the runtime.

**Expected Outcomes:**
- `PRINT` with all separator variants (`;`, `,`, no separator) works correctly
- `INPUT` parses comma-separated values into typed variables
- File OPEN modes (INPUT, OUTPUT, APPEND, RANDOM, BINARY) work via C stdio
- `EOF`, `LOC`, `LOF` return correct values

**Todo List:**
1. Implement `basic_io.c`:
   - `bs_print_int(int v)`, `bs_print_long(long v)`,
     `bs_print_float(float v)`, `bs_print_double(double v)`,
     `bs_print_string(const BString *s)`
   - `bs_print_newline()`, `bs_print_tab(int col)`, `bs_print_semicolon()`
   - `bs_print_using(const BString *fmt, ...)` — format string interpreter
   - `bs_input_int(int *v)`, `bs_input_long(long *v)`,
     `bs_input_double(double *v)`, `bs_input_string(BString *v)`
   - `bs_line_input(BString *v)` — reads entire line
   - `bs_input_prompt(const BString *prompt)` — prints prompt, no newline
2. Implement `basic_file.c`:
   - File number table: array of `{ FILE *fp; int mode; long reclen; }`
     indexed by BASIC file number (1–255)
   - `bsfile_open(int filenum, const BString *path, int mode, int reclen)`
   - `bsfile_close(int filenum)`
   - `bsfile_write_*(int filenum, ...)` — sequential WRITE # (CSV format)
   - `bsfile_read_*(int filenum, ...)` — sequential INPUT #
   - `bsfile_print_*(int filenum, ...)` — PRINT # (undelimited)
   - `bsfile_get(int filenum, long recno, void *buf, int len)` — RANDOM/BINARY
   - `bsfile_put(int filenum, long recno, const void *buf, int len)`
   - `bsfile_eof(int filenum)` → int
   - `bsfile_loc(int filenum)` → long
   - `bsfile_lof(int filenum)` → long

**Relevant Context:**
- BASIC file numbers are 1-based; store a 256-entry table indexed directly
- WRITE # format: values separated by commas, strings quoted — distinct from
  PRINT # which writes undelimited text
- RANDOM access mode needs a record length; GET/PUT use `fseek` + `fread`/`fwrite`
- `LOF` on HP-UX: use `fseek(fp, 0, SEEK_END)` + `ftell(fp)`

---

### Sub-Task 9 — Runtime Library: Math and Error Handling

**Status:** `[x] done`

**Intent:**
Implement the math built-in wrappers and the full error handling mechanism:
`TRY/CATCH/END TRY` as the primary path, plus `ON ERROR GOTO` / `RESUME NEXT`
as a legacy-compatible secondary path. Plain `RESUME` (re-execute faulting
statement) is explicitly out of scope.

**Expected Outcomes:**
- All math built-ins delegate correctly to `libm` with appropriate type coercion
- `TRY/CATCH` correctly intercepts runtime errors raised by the runtime library
- `ON ERROR GOTO label` arms a module-level fallback handler; `RESUME NEXT`
  continues past the error
- `ERR`, `ERL`, `ERROR$` report the correct error code, line, and message

**Todo List:**
1. Implement `basic_math.c` wrappers (all take/return `double` unless noted):
   - `bs_sin`, `bs_cos`, `bs_tan`, `bs_atn` (ATN = atan)
   - `bs_sqr` (sqrt), `bs_log` (natural log), `bs_exp`
   - `bs_abs_d`, `bs_abs_i`, `bs_abs_l`
   - `bs_int` (floor toward -inf), `bs_fix` (truncate toward 0)
   - `bs_rnd` (RND — seeded random 0..1), `bs_randomize`
   - `bs_sgn` → int (-1, 0, 1)
   - `bs_cint` (round to nearest integer), `bs_clng`, `bs_csng`, `bs_cdbl`
2. Implement `basic_error.c`:
   - `BsErrorFrame` struct: `{ jmp_buf jmp; int code; int line; BString msg;
     struct BsErrorFrame *prev; }` — forms a linked stack
   - Global `BsErrorFrame *bs_error_top` — current innermost handler
   - `bs_error_push(BsErrorFrame *f)` — push frame (called at TRY entry)
   - `bs_error_pop()` — pop frame (called at END TRY / normal exit)
   - `bs_raise_error(int code, int line)` — sets code/line/msg on top frame,
     calls `longjmp(bs_error_top->jmp, 1)`; if no frame active, calls
     module-level ON ERROR handler or aborts
   - Module-level ON ERROR support: separate `jmp_buf bs_on_error_jmp`,
     `int bs_on_error_active` flag, `bs_on_error_goto()`, `bs_on_error_off()`
   - `bs_resume_next()` — `longjmp` to the resume-next point stored in the
     current ON ERROR context (single point per protected block, not per statement)
   - `bs_err_code()`, `bs_erl_line()`, `bs_err_msg(BString *dst)` — accessors
     for ERR, ERL, ERROR$ built-ins
   - Error code table mapping integer codes to QuickBASIC-compatible strings
3. Wire `bs_raise_error()` into divide-by-zero (emitter wraps `/` in a check),
   string index out of range, array bounds violation, file not open, etc.

**Relevant Context:**
- `setjmp`/`longjmp` on HP-UX with gcc 4.2 works correctly — use `setjmp`/
  `longjmp`, not `sigsetjmp`/`siglongjmp` (no signal-mask manipulation needed)
- The frame stack means nested TRY/CATCH works naturally — each TRY pushes a
  frame; the innermost one catches first; END TRY pops it
- Plain `RESUME` (re-execute the faulting statement) is **not implemented** —
  this is the deliberate scope cut that avoids per-statement labeling

---

### Sub-Task 10 — Runtime Library: Arrays

**Status:** `[x] done`

**Intent:**
Implement the heap-allocated array descriptor used for dynamic `DIM` and
`REDIM` arrays, including bounds checking and optional `PRESERVE` on resize.

**Expected Outcomes:**
- `bsarray_alloc()` allocates a correctly sized block for any element type
  and dimension count
- `bsarray_realloc()` with PRESERVE copies existing data correctly
- Out-of-bounds access triggers `bs_raise_error()`

**Todo List:**
1. Define `BArray` struct: `{ void *data; int ndims; int *lbound; int *ubound;
   int elem_size; }`
2. Implement `basic_array.c`:
   - `bsarray_alloc(BArray *a, int elem_size, int ndims, int *lbounds, int *ubounds)`
   - `bsarray_free(BArray *a)`
   - `bsarray_realloc(BArray *a, int ndims, int *lbounds, int *ubounds, int preserve)`
   - `bsarray_index(const BArray *a, int *indices)` → `void*` pointer to element
   - `bsarray_check_bounds(const BArray *a, int dim, int idx)` — calls
     `bs_raise_error(9, ...)` on violation (error 9 = subscript out of range)
3. The emitter uses `bsarray_index()` result cast to the element type pointer
   for all array access

**Relevant Context:**
- BASIC arrays default to 0-based unless `OPTION BASE 1` is set — track
  `option_base` in the compiler's module state
- Static (compile-time-sized) arrays declared with constant bounds are emitted
  as plain C arrays; only runtime-sized DIM and REDIM use `BArray`

---

### Sub-Task 11 — Integration, Test Suite, and Makefile Finalization

**Status:** `[x] done`

**Intent:**
Wire all phases together in `main.c`, build the complete test suite of `.bas`
programs, and finalize the `Makefile` so a single `gmake` produces a fully
working compiler and runtime.

**Expected Outcomes:**
- `bcc hello.bas` produces `hello.c` then invokes `gcc 4.2` to produce `hello`
- `gmake test` runs all test programs and reports pass/fail
- No compiler warnings from the compiler source or runtime under `gcc 4.2 -Wall`
- The compiler correctly rejects invalid BASIC programs with useful error messages

**Todo List:**
1. Complete `compiler/main.c`:
   - Argument parsing: `bcc [options] file.bas`
   - Options: `-o outfile`, `-c` (emit C only, don't invoke gcc), `-v` (verbose),
     `--lex`, `--parse` (debug dumps)
   - Pipeline: lex → parse → semantic analysis → emit → invoke gcc
   - Invoke gcc: `gcc -o outname emitted.c -L<runtime_dir> -lbasic -lm`
2. Write test `.bas` programs (one per feature area):
   - `tests/hello.bas` — basic PRINT
   - `tests/arith.bas` — all arithmetic and comparison operators
   - `tests/strings.bas` — all string built-ins
   - `tests/control.bas` — IF, FOR, WHILE, DO, SELECT CASE
   - `tests/subs.bas` — SUB, FUNCTION, BYVAL, BYREF, recursion
   - `tests/arrays.bas` — DIM static, DIM dynamic, REDIM PRESERVE
   - `tests/types.bas` — TYPE / END TYPE, field access
   - `tests/files.bas` — OPEN/CLOSE/WRITE/READ/EOF
   - `tests/try_catch.bas` — TRY/CATCH nesting, re-raise, ERR/ERROR$
   - `tests/on_error.bas` — ON ERROR GOTO, RESUME NEXT, ERR, ERL
   - `tests/math.bas` — all math built-ins
3. Write `tests/run_tests.sh` — compile and run each test, compare output to
   expected, print PASS/FAIL
4. Finalize `Makefile`:
   - Dependency tracking via `.d` files (`gcc -MMD`)
   - `install` target copies `bcc` and `libbasic.a` to `/usr/local/bin` and
     `/usr/local/lib`
5. Fix all warnings found during integration

**Relevant Context:**
- On HP-UX, the default `sh` may not support all bash-isms — keep
  `run_tests.sh` POSIX `/bin/sh` compatible
- `gcc 4.2` on HP-UX may need `-lm` explicitly; ensure the Makefile and the
  compiler's gcc invocation both pass it
- The emitted `.c` file should be placed in `/tmp` or next to the `.bas` file
  depending on the `-c` flag

---

## Implementation Order

```
Sub-Task 1  →  Sub-Task 2  →  Sub-Task 3  →  Sub-Task 4
                                                  ↓
Sub-Task 5  ←────────────────────────────────────┘
    ↓
Sub-Task 6  ←── (depends on 7, 8, 9, 10 for runtime API knowledge)
    ↓
Sub-Tasks 7, 8, 9, 10  (runtime — can be done in parallel conceptually,
                         but implement one at a time for focus)
    ↓
Sub-Task 11 (integration)
```

Sub-Tasks 7–10 can be started in any order after Sub-Task 6's emitter API
is sketched, since they are independent C files with a defined interface.
