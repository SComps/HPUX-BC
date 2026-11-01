' tests/inkey.bas -- Test INKEY$ non-blocking keyboard read
'
' Run with: bcc tests/inkey.bas && ./inkey
'
' The program loops for up to 5 seconds waiting for a keypress.
' Press any key to test; press Q to quit early.

PRINT "=== INKEY$ Test ==="
PRINT "Press any key (Q to quit, or wait 5 seconds)..."
PRINT ""

DIM k AS STRING
DIM i AS INTEGER
DIM done AS INTEGER

done = 0
i = 0

' Poll loop: spin up to ~500000 iterations (~5 seconds on HP-UX PA-RISC)
DO WHILE i < 500000 AND done = 0
    k = INKEY$
    IF LEN(k) > 0 THEN
        IF LEN(k) = 1 THEN
            IF ASC(k) = 0 THEN
                ' Extended key (second byte is scan code) -- can't happen
                ' in a 1-char result; handled below
                PRINT "Control character: ASC = "; ASC(k)
            ELSE
                PRINT "Key pressed: '"; k; "' (ASC="; ASC(k); ")"
            END IF
        ELSEIF LEN(k) = 2 AND ASC(k) = 0 THEN
            ' Extended key: NUL + scancode
            DIM scan AS INTEGER
            scan = ASC(MID$(k, 2, 1))
            SELECT CASE scan
                CASE 72: PRINT "Up arrow"
                CASE 80: PRINT "Down arrow"
                CASE 75: PRINT "Left arrow"
                CASE 77: PRINT "Right arrow"
                CASE 59: PRINT "F1"
                CASE 60: PRINT "F2"
                CASE 61: PRINT "F3"
                CASE 62: PRINT "F4"
                CASE 63: PRINT "F5"
                CASE ELSE
                    PRINT "Extended key, scan="; scan
            END SELECT
        ELSE
            PRINT "Multi-char key sequence, LEN="; LEN(k)
        END IF

        IF UCASE$(k) = "Q" THEN done = 1
    END IF
    i = i + 1
LOOP

IF done = 0 THEN
    PRINT "Timeout -- no key pressed."
ELSE
    PRINT "Quit."
END IF

PRINT "=== INKEY$ Test Done ==="
