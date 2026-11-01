' files.bas -- file I/O: OPEN, WRITE, INPUT, CLOSE
DIM f AS INTEGER
f = 1
OPEN "test_output.tmp" FOR OUTPUT AS #f
WRITE #f, "Hello"
WRITE #f, 42
CLOSE #f

OPEN "test_output.tmp" FOR INPUT AS #f
DIM s AS STRING
DIM n AS INTEGER
INPUT #f, s
INPUT #f, n
CLOSE #f
PRINT s
PRINT n

' cleanup: truncate/overwrite the temp file
OPEN "test_output.tmp" FOR OUTPUT AS #f
CLOSE #f
