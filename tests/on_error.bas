' on_error.bas -- ON ERROR GOTO, RESUME NEXT, ERR, ERL
ON ERROR GOTO ErrHandler
PRINT "before error"
DIM x AS INTEGER
x = 1 \ 0    ' division by zero
PRINT "after error (should not print)"
GOTO Done

ErrHandler:
PRINT "Error number:"; ERR
RESUME NEXT

Done:
PRINT "done"
