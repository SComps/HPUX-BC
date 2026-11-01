' strings.bas -- string built-ins
DIM s AS STRING
DIM t AS STRING
s = "Hello, World!"
PRINT LEN(s)
PRINT LEFT$(s, 5)
PRINT RIGHT$(s, 6)
PRINT MID$(s, 8, 5)
PRINT UCASE$(s)
PRINT LCASE$(s)
t = "World"
PRINT INSTR(s, t)
PRINT STR$(42)
PRINT VAL("3.14")
PRINT ASC("A")
PRINT CHR$(65)
PRINT SPACE$(5) & "X"
PRINT TRIM$("  hello  ")
