REM ============================================================
REM  primes3.bas - Sieve of Eratosthenes
REM  Computes all prime numbers up to a limit chosen by the user.
REM  Written for the HP-UX BASIC compiler (bcc).
REM  Build:  bcc primes3.bas      Run:  ./primes3
REM ============================================================

DIM maxLimit AS LONG
maxLimit = 50000000          ' sanity cap (about 200 MB for the sieve)

' Returns n with thousands separators, e.g. 1234567 -> "1,234,567"
FUNCTION Commas(n AS LONG) AS STRING
    DIM s AS STRING
    DIM result AS STRING
    DIM i AS INTEGER
    DIM k AS INTEGER
    s = TRIM$(STR$(n))
    result = ""
    k = 0
    FOR i = LEN(s) TO 1 STEP -1
        IF k > 0 AND k MOD 3 = 0 THEN result = "," & result
        result = MID$(s, i, 1) & result
        k = k + 1
    NEXT i
    Commas = result
END FUNCTION

SUB RunSieve(n AS LONG, showList AS INTEGER)
    DIM i AS LONG
    DIM j AS LONG
    DIM root AS LONG
    DIM count AS LONG
    DIM lastPrime AS LONG
    DIM perLine AS INTEGER
    DIM col AS INTEGER
    DIM sieve(n) AS INTEGER          ' dynamic array: 0 = prime, 1 = composite

    TRY
        PRINT
        PRINT "Working..."

        FOR i = 0 TO n
            sieve(i) = 0
        NEXT i

        ' Cross out multiples of each prime up to sqrt(n)
        root = INT(SQR(n))
        FOR i = 2 TO root
            IF sieve(i) = 0 THEN
                FOR j = i * i TO n STEP i
                    sieve(j) = 1
                NEXT j
            END IF
        NEXT i

        ' Each grid cell is 11 characters wide plus 2 spaces of gap
        perLine = TERMCOLS \ 13
        IF perLine < 1 THEN perLine = 1
        col = 0
        count = 0
        lastPrime = 0

        IF showList <> 0 THEN
            PRINT
            PRINT "Primes up to "; Commas(n)
            PRINT STRING$(40, 45)
        END IF

        FOR i = 2 TO n
            IF sieve(i) = 0 THEN
                count = count + 1
                lastPrime = i
                IF showList <> 0 THEN
                    PRINT USING "###,###,###"; TRIM$(STR$(i));
                    PRINT "  ";
                    col = col + 1
                    IF col >= perLine THEN
                        PRINT
                        col = 0
                    END IF
                END IF
            END IF
        NEXT i
        IF showList <> 0 AND col > 0 THEN PRINT

        PRINT
        PRINT STRING$(40, 45)
        PRINT "Range searched : 2 to "; Commas(n)
        PRINT "Primes found   : "; Commas(count)
        IF count > 0 THEN
            PRINT "Largest prime  : "; Commas(lastPrime)
        END IF
        PRINT STRING$(40, 45)
    CATCH e AS STRING
        PRINT "Error during calculation: "; e
    END TRY
END SUB

' ---------------- main program ----------------
DIM limit AS LONG
DIM answer AS STRING
DIM again AS STRING
DIM showList AS INTEGER

PRINT "================================="
PRINT "   Prime Number Sieve (bcc)"
PRINT "================================="

DO
    PRINT
    INPUT "Find primes up to what number? ", limit

    IF limit < 2 THEN
        PRINT "Please enter a number of 2 or greater."
    ELSEIF limit > maxLimit THEN
        PRINT "That is too large; the maximum is "; Commas(maxLimit); "."
    ELSE
        INPUT "List the primes (Y/N)? ", answer
        answer = UCASE$(TRIM$(answer))
        IF LEFT$(answer, 1) = "Y" THEN
            showList = -1
        ELSE
            showList = 0
        END IF
        RunSieve limit, showList
    END IF

    PRINT
    INPUT "Run again (Y/N)? ", again
    again = UCASE$(TRIM$(again))
LOOP WHILE LEFT$(again, 1) = "Y"

PRINT "Goodbye."
END
