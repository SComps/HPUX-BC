' arrays.bas -- static and dynamic arrays
DIM a(5) AS INTEGER
DIM i AS INTEGER
FOR i = 1 TO 5
    a(i) = i * i
NEXT i
FOR i = 1 TO 5
    PRINT a(i)
NEXT i

DIM n AS INTEGER
n = 3
DIM c(n) AS INTEGER
FOR i = 1 TO n
    c(i) = i
NEXT i
PRINT c(1); c(2); c(3)
