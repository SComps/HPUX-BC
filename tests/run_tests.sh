#!/bin/sh
# run_tests.sh -- BASIC compiler test runner
# Usage: ./run_tests.sh [path_to_bcc]
#
# POSIX /bin/sh only -- no bash-isms, no [[ ]], no arrays.
# Arithmetic via $(( )) which is POSIX sh (not bash-specific).

BCC=${1:-../bcc}
PASS=0
FAIL=0

# Change to the directory containing this script so relative paths work.
SCRIPT_DIR=$(dirname "$0")
cd "$SCRIPT_DIR" || exit 1

run_test() {
    name=$1
    src=$2

    # Compile
    "$BCC" -o /tmp/bcc_test "$src" 2>/tmp/bcc_test.err
    if [ $? -ne 0 ]; then
        echo "FAIL (compile): $name"
        cat /tmp/bcc_test.err
        FAIL=$((FAIL + 1))
        return
    fi

    # Run
    /tmp/bcc_test >/tmp/bcc_test.out 2>&1
    if [ $? -ne 0 ]; then
        echo "FAIL (run): $name"
        cat /tmp/bcc_test.out
        FAIL=$((FAIL + 1))
        return
    fi

    echo "PASS: $name"
    PASS=$((PASS + 1))
}

run_test "hello"     hello.bas
run_test "arith"     arith.bas
run_test "strings"   strings.bas
run_test "control"   control.bas
run_test "subs"      subs.bas
run_test "arrays"    arrays.bas
run_test "types"     types.bas
run_test "math"      math.bas
run_test "try_catch" try_catch.bas
run_test "on_error"  on_error.bas
run_test "files"     files.bas

echo ""
echo "Results: $PASS passed, $FAIL failed"

# Clean up temp files
rm -f /tmp/bcc_test /tmp/bcc_test.out /tmp/bcc_test.err

# Exit non-zero if any test failed
[ $FAIL -eq 0 ]
