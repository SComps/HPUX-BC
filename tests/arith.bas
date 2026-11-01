' arith.bas -- arithmetic and comparison operators
DIM a AS INTEGER
DIM b AS INTEGER
a = 10
b = 3
PRINT a + b
PRINT a - b
PRINT a * b
PRINT a \ b      ' integer divide = 3
PRINT a MOD b    ' = 1
PRINT a ^ 2      ' = 100
IF a > b THEN PRINT "a > b" ELSE PRINT "a <= b"
IF a = 10 THEN PRINT "a is ten"
PRINT a <> b     ' = -1 (true in BASIC)
