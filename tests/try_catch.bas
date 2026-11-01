' try_catch.bas -- TRY/CATCH error handling
TRY
    PRINT "before error"
    DIM a(5) AS INTEGER
    PRINT a(10)   ' subscript out of range
    PRINT "after error (should not print)"
CATCH e AS STRING
    PRINT "Caught: "; e
END TRY
PRINT "after try block"
