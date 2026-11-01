' types.bas -- user-defined TYPE / END TYPE, field access
TYPE Point
    x AS INTEGER
    y AS INTEGER
END TYPE

DIM p AS Point
p.x = 10
p.y = 20
PRINT p.x; p.y
PRINT p.x + p.y
