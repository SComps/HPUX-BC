' control.bas -- control flow: FOR, WHILE, DO, SELECT CASE
DIM i AS INTEGER
FOR i = 1 TO 5
    PRINT i
NEXT i

i = 0
WHILE i < 3
    i = i + 1
    PRINT "while"; i
WEND

i = 0
DO
    i = i + 1
    PRINT "do"; i
LOOP UNTIL i >= 3

SELECT CASE i
    CASE 1
        PRINT "one"
    CASE 2
        PRINT "two"
    CASE 3
        PRINT "three"
    CASE ELSE
        PRINT "other"
END SELECT
