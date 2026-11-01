' subs.bas -- SUBs and FUNCTIONs, including recursion
SUB Greet(name AS STRING)
    PRINT "Hello, "; name; "!"
END SUB

FUNCTION Add(a AS INTEGER, b AS INTEGER) AS INTEGER
    Add = a + b
END FUNCTION

FUNCTION Factorial(n AS INTEGER) AS LONG
    IF n <= 1 THEN
        Factorial = 1
    ELSE
        Factorial = n * Factorial(n - 1)
    END IF
END FUNCTION

Greet "BASIC"
PRINT Add(3, 4)
PRINT Factorial(10)
