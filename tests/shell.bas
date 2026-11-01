' tests/shell.bas -- Test SHELL, SHELL$, and ERRLEVEL
'
' Run with: bcc tests/shell.bas && ./shell

PRINT "=== SHELL Test ==="

' Simple SHELL with a command
PRINT "Running: echo Hello from SHELL"
SHELL "echo Hello from SHELL"
PRINT "ERRLEVEL after echo ="; ERRLEVEL
PRINT ""

' SHELL$ to capture output
DIM result AS STRING
result = SHELL$("echo Captured output")
PRINT "SHELL$ result = "; result

' SHELL$ with a multi-line command (ls -1 of /tmp)
PRINT "Files in /tmp (first 3 lines):"
DIM listing AS STRING
listing = SHELL$("ls -1 /tmp 2>/dev/null | head -3")
PRINT listing

' Test ERRLEVEL with a failing command
SHELL "false"
PRINT "ERRLEVEL after false ="; ERRLEVEL

SHELL "true"
PRINT "ERRLEVEL after true ="; ERRLEVEL

' SHELL$ with uname
DIM osname AS STRING
osname = SHELL$("uname -s")
PRINT "OS = "; osname

PRINT "=== SHELL Test Done ==="
