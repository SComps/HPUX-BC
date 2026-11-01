' ide.bas -- HPUX-BC Integrated Development Environment
'
' A full-screen text editor and BASIC compiler front-end.
' Written in HPUX-BC BASIC; compiled with bcc.
'
' Screen layout (80x24):
'   Row 1        : Title bar  (filename, modified flag)
'   Rows 2..22   : Editor area (EDIT_ROWS = 21 visible lines)
'   Row 23       : Message bar (compile errors, prompts)
'   Row 24       : Key help bar
'
' Key bindings:
'   Arrows       : Move cursor
'   Home / End   : Start / end of line
'   PgUp / PgDn  : Scroll one screen
'   Backspace    : Delete char left
'   Delete       : Delete char right  (Ctrl+D)
'   Enter        : Insert newline / split line
'   Ctrl+Y       : Delete current line
'   Ctrl+K       : Copy (yank) current line to clipboard
'   Ctrl+U       : Paste clipboard line below current
'   Ctrl+S       : Save           (also F2)
'   Ctrl+O       : Open file      (also F3)
'   Ctrl+N       : New file       (also F4)
'   Ctrl+R       : Compile + Run  (also F5)
'   Ctrl+B       : Compile / Build only (also F6)
'   Ctrl+Q / ^X  : Quit           (also F10)
'
' Compile via:  bcc <file> -o /tmp/ide_bin 2>/tmp/ide_err
' Run via:      SHELL "/tmp/ide_bin"
'
' Limitations (first version):
'   - Tab is stored as 4 spaces on input
'   - No undo / redo
'   - Lines longer than 256 chars are truncated on load
'   - Max 2000 lines

' ------------------------------------------------------------------
' Named constants implemented as global integer variables
' (CONST keyword is not yet supported; use DIM + assignment)
' ------------------------------------------------------------------
DIM SCREEN_COLS  AS INTEGER
DIM SCREEN_ROWS  AS INTEGER
DIM EDIT_TOP     AS INTEGER
DIM EDIT_BOT     AS INTEGER
DIM EDIT_ROWS    AS INTEGER
DIM MSG_ROW      AS INTEGER
DIM HELP_ROW     AS INTEGER
DIM MAX_LINES    AS INTEGER
DIM MAX_LINE_LEN AS INTEGER
DIM COL_TITLE    AS INTEGER
DIM COL_EDIT_FG  AS INTEGER
DIM COL_MSG_FG   AS INTEGER
DIM COL_HELP_FG  AS INTEGER

' ------------------------------------------------------------------
' Global editor state
' ------------------------------------------------------------------
DIM lines(2000) AS STRING        ' line buffer (1-based)
DIM lineCount        AS INTEGER  ' number of lines in buffer
DIM curRow           AS INTEGER  ' cursor row in buffer (1-based)
DIM curCol           AS INTEGER  ' cursor col in line   (1-based)
DIM topLine          AS INTEGER  ' first visible line   (1-based)
DIM fileName         AS STRING   ' current file path
DIM modified         AS INTEGER  ' 1 = unsaved changes
DIM clipboard        AS STRING   ' single-line clipboard (Ctrl+K/U)
DIM running          AS INTEGER  ' main loop flag
DIM msgText          AS STRING   ' current message bar text
DIM promptCancelled  AS INTEGER  ' set to 1 by PromptFileName on ESC

' ------------------------------------------------------------------
' MAIN PROGRAM
' ------------------------------------------------------------------

' Initialise named constants from actual terminal dimensions
SCREEN_COLS  = TERMCOLS
SCREEN_ROWS  = TERMROWS
IF SCREEN_COLS < 40 THEN SCREEN_COLS = 80
IF SCREEN_ROWS < 10 THEN SCREEN_ROWS = 24
EDIT_TOP     = 2
EDIT_BOT     = SCREEN_ROWS - 2
EDIT_ROWS    = EDIT_BOT - EDIT_TOP + 1
MSG_ROW      = SCREEN_ROWS - 1
HELP_ROW     = SCREEN_ROWS
MAX_LINES    = 2000
MAX_LINE_LEN = 256
COL_TITLE    = 3    ' cyan
COL_EDIT_FG  = 7    ' light grey
COL_MSG_FG   = 14   ' bright yellow
COL_HELP_FG  = 11   ' bright cyan

' Parse command-line argument if provided (via COMMAND$(1) or $BCC_IDE_FILE)
DIM cmdArg AS STRING
fileName = ""
cmdArg = TRIM$(COMMAND$(1))
IF LEN(cmdArg) = 0 THEN
    cmdArg = TRIM$(SHELL$("echo $BCC_IDE_FILE 2>/dev/null"))
    IF cmdArg = "$BCC_IDE_FILE" THEN cmdArg = ""
END IF
IF LEN(cmdArg) > 0 THEN
    fileName = cmdArg
END IF

' Initialise buffer with one empty line
CALL InitBuffer

' Enter full-screen mode before loading so SetMsg displays cleanly
CLS

' Load file if given
IF LEN(fileName) > 0 THEN
    CALL LoadFile(fileName)
ELSE
    fileName = "untitled.bas"
END IF

CALL DrawAll

running = 1
DO WHILE running = 1
    CALL HandleKey
LOOP

' Restore terminal
CLS
PRINT "Goodbye."

END

' ==================================================================
' SUB InitBuffer
'   Reset the line buffer to one empty line.
' ==================================================================
SUB InitBuffer
    lineCount  = 1
    lines(1)   = ""
    curRow     = 1
    curCol     = 1
    topLine    = 1
    modified   = 0
    clipboard  = ""
    msgText    = ""
END SUB

' ==================================================================
' SUB DrawAll
'   Full redraw: title bar, all visible editor lines, status, help.
' ==================================================================
SUB DrawAll
    SCREEN_COLS = TERMCOLS
    SCREEN_ROWS = TERMROWS
    IF SCREEN_COLS < 40 THEN SCREEN_COLS = 80
    IF SCREEN_ROWS < 10 THEN SCREEN_ROWS = 24
    EDIT_TOP    = 2
    EDIT_BOT    = SCREEN_ROWS - 2
    EDIT_ROWS   = EDIT_BOT - EDIT_TOP + 1
    MSG_ROW     = SCREEN_ROWS - 1
    HELP_ROW    = SCREEN_ROWS
    CALL DrawTitleBar
    CALL DrawEditArea
    CALL DrawMsgBar
    CALL DrawHelpBar
    CALL PlaceCursor
END SUB

' ==================================================================
' SUB DrawTitleBar
' ==================================================================
SUB DrawTitleBar
    DIM title AS STRING
    DIM pad   AS INTEGER

    COLOR COL_TITLE
    LOCATE 1, 1
    title = " HPUX-BC IDE  "
    IF LEN(fileName) > 0 THEN
        title = title + fileName
    ELSE
        title = title + "(no file)"
    END IF
    IF modified = 1 THEN
        title = title + " [modified]"
    END IF
    ' Pad to full width
    pad = SCREEN_COLS - LEN(title)
    IF pad > 0 THEN
        title = title + SPACE$(pad)
    ELSE
        title = LEFT$(title, SCREEN_COLS)
    END IF
    PRINT title;
    COLOR COL_EDIT_FG
END SUB

' ==================================================================
' SUB DrawEditArea
'   Redraw all EDIT_ROWS visible lines.
' ==================================================================
SUB DrawEditArea
    DIM r    AS INTEGER   ' screen row (1-based)
    DIM ln   AS INTEGER   ' buffer line index
    DIM s    AS STRING
    DIM pad  AS INTEGER

    COLOR COL_EDIT_FG
    FOR r = 1 TO EDIT_ROWS
        ln = topLine + r - 1
        LOCATE EDIT_TOP + r - 1, 1
        IF ln <= lineCount THEN
            s = lines(ln)
            IF LEN(s) > SCREEN_COLS THEN
                s = LEFT$(s, SCREEN_COLS)
            END IF
            pad = SCREEN_COLS - LEN(s)
            IF pad > 0 THEN
                PRINT s + SPACE$(pad);
            ELSE
                PRINT s;
            END IF
        ELSE
            ' Past end of buffer: blank row with tilde
            PRINT "~" + SPACE$(SCREEN_COLS - 1);
        END IF
    NEXT r
END SUB

' ==================================================================
' SUB DrawOneLine(bufLine)
'   Redraw a single editor line (1-based buffer index).
' ==================================================================
SUB DrawOneLine(bufLine AS INTEGER)
    DIM r   AS INTEGER
    DIM s   AS STRING
    DIM pad AS INTEGER

    r = bufLine - topLine + 1
    IF r < 1 OR r > EDIT_ROWS THEN EXIT SUB

    LOCATE EDIT_TOP + r - 1, 1
    COLOR COL_EDIT_FG
    IF bufLine <= lineCount THEN
        s = lines(bufLine)
        IF LEN(s) > SCREEN_COLS THEN
            s = LEFT$(s, SCREEN_COLS)
        END IF
        pad = SCREEN_COLS - LEN(s)
        IF pad > 0 THEN
            PRINT s + SPACE$(pad);
        ELSE
            PRINT s;
        END IF
    ELSE
        PRINT "~" + SPACE$(SCREEN_COLS - 1);
    END IF
END SUB

' ==================================================================
' SUB DrawMsgBar
' ==================================================================
SUB DrawMsgBar
    DIM s   AS STRING
    DIM pad AS INTEGER

    COLOR COL_MSG_FG
    LOCATE MSG_ROW, 1
    s = " " + msgText
    IF LEN(s) > SCREEN_COLS THEN
        s = LEFT$(s, SCREEN_COLS)
    END IF
    pad = SCREEN_COLS - LEN(s)
    IF pad > 0 THEN
        PRINT s + SPACE$(pad);
    ELSE
        PRINT s;
    END IF
    COLOR COL_EDIT_FG
END SUB

' ==================================================================
' SUB DrawHelpBar
' ==================================================================
SUB DrawHelpBar
    DIM s   AS STRING
    DIM pad AS INTEGER

    COLOR COL_HELP_FG
    LOCATE HELP_ROW, 1
    s = " ^S=Save  ^O=Open  ^N=New  ^R=Run  ^B=Build  ^K/^U=Copy/Paste  ^Q=Quit"
    IF LEN(s) > SCREEN_COLS THEN
        s = LEFT$(s, SCREEN_COLS)
    ELSE
        pad = SCREEN_COLS - LEN(s)
        s = s + SPACE$(pad)
    END IF
    PRINT s;
    COLOR COL_EDIT_FG
END SUB

' ==================================================================
' SUB SetMsg(txt)
'   Update the message bar.
' ==================================================================
SUB SetMsg(txt AS STRING)
    msgText = txt
    CALL DrawMsgBar
    CALL PlaceCursor
END SUB

' ==================================================================
' SUB PlaceCursor
'   Move the terminal cursor to the current editor position.
' ==================================================================
SUB PlaceCursor
    DIM screenRow AS INTEGER
    DIM screenCol AS INTEGER
    screenRow = curRow - topLine + EDIT_TOP
    screenCol = curCol
    IF screenRow < EDIT_TOP THEN screenRow = EDIT_TOP
    IF screenRow > EDIT_BOT THEN screenRow = EDIT_BOT
    IF screenCol < 1 THEN screenCol = 1
    IF screenCol > SCREEN_COLS THEN screenCol = SCREEN_COLS
    LOCATE screenRow, screenCol
END SUB

' ==================================================================
' SUB HandleKey
'   Read one key from INKEY$ and dispatch.
' ==================================================================
SUB HandleKey
    DIM k    AS STRING
    DIM code AS INTEGER
    DIM scan AS INTEGER

    ' Spin until a key arrives
    k = ""
    DO WHILE LEN(k) = 0
        k = INKEY$
    LOOP

    IF LEN(k) = 1 THEN
        code = ASC(k)
        ' Extended key via NUL prefix
        IF code = 0 THEN
            ' Second byte comes in the same INKEY$ read (2-char string)
            ' This branch won't fire for 1-char result -- see LEN=2 below
            EXIT SUB
        END IF
        SELECT CASE code
            CASE 13        ' KEY_ENTER
                CALL DoEnter
            CASE 8         ' KEY_BACKSPACE
                CALL DoBackspace
            CASE 4         ' KEY_CTRL_D (Delete char right)
                CALL DoDeleteCharRight
            CASE 25        ' KEY_CTRL_Y (Delete line)
                CALL DoDeleteLine
            CASE 11        ' KEY_CTRL_K (Copy line)
                CALL DoCopyLine
            CASE 21        ' KEY_CTRL_U (Paste line)
                CALL DoPasteLine
            CASE 19        ' KEY_CTRL_S (Save)
                CALL DoSave
            CASE 15        ' KEY_CTRL_O (Open)
                CALL DoOpen
            CASE 14        ' KEY_CTRL_N (New)
                CALL DoNew
            CASE 18        ' KEY_CTRL_R (Compile + Run)
                CALL DoCompileRun
            CASE 2         ' KEY_CTRL_B (Build / Compile only)
                CALL DoCompileOnly
            CASE 17, 24    ' KEY_CTRL_Q / KEY_CTRL_X (Quit)
                CALL DoQuit
            CASE 9         ' KEY_TAB
                CALL DoTab
            CASE 27        ' KEY_ESC
                CALL SetMsg("")
            CASE ELSE
                ' Printable character
                IF code >= 32 AND code <= 126 THEN
                    CALL DoInsertChar(k)
                END IF
        END SELECT
    ELSEIF LEN(k) = 2 AND ASC(k) = 0 THEN
        scan = ASC(MID$(k, 2, 1))
        SELECT CASE scan
            CASE 72:    CALL MoveCursorUp      ' SCAN_UP
            CASE 80:    CALL MoveCursorDown    ' SCAN_DOWN
            CASE 75:    CALL MoveCursorLeft    ' SCAN_LEFT
            CASE 77:    CALL MoveCursorRight   ' SCAN_RIGHT
            CASE 71:    CALL MoveCursorHome    ' SCAN_HOME
            CASE 79:    CALL MoveCursorEnd     ' SCAN_END
            CASE 73:    CALL MoveCursorPgUp    ' SCAN_PGUP
            CASE 81:    CALL MoveCursorPgDn    ' SCAN_PGDN
            CASE 83:    CALL DoDeleteCharRight ' SCAN_DEL
            CASE 60:    CALL DoSave            ' SCAN_F2
            CASE 61:    CALL DoOpen            ' SCAN_F3
            CASE 62:    CALL DoNew             ' SCAN_F4
            CASE 63:    CALL DoCompileRun      ' SCAN_F5
            CASE 64:    CALL DoCompileOnly     ' SCAN_F6
            CASE 68:    CALL DoQuit            ' SCAN_F10
        END SELECT
    END IF
END SUB

' ==================================================================
' Cursor movement SUBs
' ==================================================================
SUB MoveCursorUp
    IF curRow > 1 THEN
        curRow = curRow - 1
        IF curCol > LEN(lines(curRow)) + 1 THEN
            curCol = LEN(lines(curRow)) + 1
        END IF
        IF curRow < topLine THEN
            topLine = curRow
            CALL DrawEditArea
        END IF
    END IF
    CALL PlaceCursor
END SUB

SUB MoveCursorDown
    IF curRow < lineCount THEN
        curRow = curRow + 1
        IF curCol > LEN(lines(curRow)) + 1 THEN
            curCol = LEN(lines(curRow)) + 1
        END IF
        IF curRow >= topLine + EDIT_ROWS THEN
            topLine = curRow - EDIT_ROWS + 1
            CALL DrawEditArea
        END IF
    END IF
    CALL PlaceCursor
END SUB

SUB MoveCursorLeft
    IF curCol > 1 THEN
        curCol = curCol - 1
    ELSEIF curRow > 1 THEN
        curRow = curRow - 1
        curCol = LEN(lines(curRow)) + 1
        IF curRow < topLine THEN
            topLine = curRow
            CALL DrawEditArea
        END IF
    END IF
    CALL PlaceCursor
END SUB

SUB MoveCursorRight
    IF curCol <= LEN(lines(curRow)) THEN
        curCol = curCol + 1
    ELSEIF curRow < lineCount THEN
        curRow = curRow + 1
        curCol = 1
        IF curRow >= topLine + EDIT_ROWS THEN
            topLine = curRow - EDIT_ROWS + 1
            CALL DrawEditArea
        END IF
    END IF
    CALL PlaceCursor
END SUB

SUB MoveCursorHome
    curCol = 1
    CALL PlaceCursor
END SUB

SUB MoveCursorEnd
    curCol = LEN(lines(curRow)) + 1
    IF curCol > SCREEN_COLS THEN curCol = SCREEN_COLS
    CALL PlaceCursor
END SUB

SUB MoveCursorPgUp
    IF topLine > EDIT_ROWS THEN
        topLine = topLine - EDIT_ROWS
    ELSE
        topLine = 1
    END IF
    curRow = topLine
    curCol = 1
    CALL DrawEditArea
    CALL PlaceCursor
END SUB

SUB MoveCursorPgDn
    topLine = topLine + EDIT_ROWS
    IF topLine > lineCount THEN topLine = lineCount
    curRow = topLine
    curCol = 1
    CALL DrawEditArea
    CALL PlaceCursor
END SUB

' ==================================================================
' Editing SUBs
' ==================================================================

' DoInsertChar -- insert a printable character at cursor position
SUB DoInsertChar(ch AS STRING)
    DIM ln  AS STRING
    DIM pre AS STRING
    DIM suf AS STRING

    ln  = lines(curRow)
    IF LEN(ln) >= MAX_LINE_LEN THEN EXIT SUB
    pre = LEFT$(ln, curCol - 1)
    suf = MID$(ln, curCol)
    lines(curRow) = pre + ch + suf
    curCol = curCol + 1
    modified = 1
    CALL DrawOneLine(curRow)
    CALL DrawTitleBar
    CALL PlaceCursor
END SUB

' DoTab -- insert 4 spaces
SUB DoTab
    DIM i AS INTEGER
    FOR i = 1 TO 4
        CALL DoInsertChar(" ")
    NEXT i
END SUB

' DoEnter -- split line at cursor
SUB DoEnter
    DIM ln   AS STRING
    DIM pre  AS STRING
    DIM suf  AS STRING
    DIM i    AS INTEGER

    IF lineCount >= MAX_LINES THEN
        CALL SetMsg("Buffer full (max " + STR$(MAX_LINES) + " lines)")
        EXIT SUB
    END IF

    ln  = lines(curRow)
    pre = LEFT$(ln, curCol - 1)
    suf = MID$(ln, curCol)

    ' Shift lines down from lineCount to curRow+1
    FOR i = lineCount TO curRow + 1 STEP -1
        lines(i + 1) = lines(i)
    NEXT i
    lineCount = lineCount + 1

    lines(curRow)     = pre
    lines(curRow + 1) = suf
    curRow = curRow + 1
    curCol = 1

    IF curRow >= topLine + EDIT_ROWS THEN
        topLine = curRow - EDIT_ROWS + 1
    END IF

    modified = 1
    CALL DrawEditArea
    CALL DrawTitleBar
    CALL PlaceCursor
END SUB

' DoBackspace -- delete character to the left
SUB DoBackspace
    DIM ln  AS STRING
    DIM pre AS STRING
    DIM suf AS STRING
    DIM i   AS INTEGER

    IF curCol > 1 THEN
        ' Delete char within current line
        ln  = lines(curRow)
        pre = LEFT$(ln, curCol - 2)
        suf = MID$(ln, curCol)
        lines(curRow) = pre + suf
        curCol = curCol - 1
        modified = 1
        CALL DrawOneLine(curRow)
        CALL DrawTitleBar
        CALL PlaceCursor
    ELSEIF curRow > 1 THEN
        ' Merge with previous line
        DIM prevLen AS INTEGER
        prevLen = LEN(lines(curRow - 1))
        lines(curRow - 1) = lines(curRow - 1) + lines(curRow)
        ' Shift lines up
        FOR i = curRow TO lineCount - 1
            lines(i) = lines(i + 1)
        NEXT i
        lines(lineCount) = ""
        lineCount = lineCount - 1
        curRow = curRow - 1
        curCol = prevLen + 1
        IF curRow < topLine THEN
            topLine = curRow
        END IF
        modified = 1
        CALL DrawEditArea
        CALL DrawTitleBar
        CALL PlaceCursor
    END IF
END SUB

' DoDeleteCharRight -- delete character under cursor (Del / Ctrl+D)
SUB DoDeleteCharRight
    DIM ln  AS STRING
    DIM pre AS STRING
    DIM suf AS STRING
    DIM i   AS INTEGER

    ln = lines(curRow)
    IF curCol <= LEN(ln) THEN
        ' Delete char at curCol
        pre = LEFT$(ln, curCol - 1)
        suf = MID$(ln, curCol + 1)
        lines(curRow) = pre + suf
        modified = 1
        CALL DrawOneLine(curRow)
        CALL DrawTitleBar
        CALL PlaceCursor
    ELSEIF curRow < lineCount THEN
        ' At end of line: merge next line in
        lines(curRow) = lines(curRow) + lines(curRow + 1)
        FOR i = curRow + 1 TO lineCount - 1
            lines(i) = lines(i + 1)
        NEXT i
        lines(lineCount) = ""
        lineCount = lineCount - 1
        modified = 1
        CALL DrawEditArea
        CALL DrawTitleBar
        CALL PlaceCursor
    END IF
END SUB

' DoDeleteLine -- Ctrl+Y: delete current line
SUB DoDeleteLine
    DIM i AS INTEGER
    IF lineCount = 1 THEN
        ' Last line: just blank it
        lines(1) = ""
        curCol = 1
    ELSE
        FOR i = curRow TO lineCount - 1
            lines(i) = lines(i + 1)
        NEXT i
        lines(lineCount) = ""
        lineCount = lineCount - 1
        IF curRow > lineCount THEN curRow = lineCount
        curCol = 1
    END IF
    modified = 1
    CALL DrawEditArea
    CALL DrawTitleBar
    CALL PlaceCursor
END SUB

' DoCopyLine -- Ctrl+K: copy (yank) current line to clipboard
SUB DoCopyLine
    clipboard = lines(curRow)
    CALL SetMsg("Line copied to clipboard")
END SUB

' DoPasteLine -- Ctrl+U: paste clipboard below current line
SUB DoPasteLine
    DIM i AS INTEGER

    IF lineCount >= MAX_LINES THEN
        CALL SetMsg("Buffer full")
        EXIT SUB
    END IF

    ' Insert blank line after curRow
    FOR i = lineCount TO curRow + 1 STEP -1
        lines(i + 1) = lines(i)
    NEXT i
    lineCount = lineCount + 1
    lines(curRow + 1) = clipboard

    curRow = curRow + 1
    curCol = 1
    IF curRow >= topLine + EDIT_ROWS THEN
        topLine = curRow - EDIT_ROWS + 1
    END IF
    modified = 1
    CALL DrawEditArea
    CALL DrawTitleBar
    CALL PlaceCursor
    CALL SetMsg("Line pasted")
END SUB

' ==================================================================
' File operations
' ==================================================================

' DoSave -- F2: save current file
SUB DoSave
    IF fileName = "" OR fileName = "untitled.bas" THEN
        CALL PromptFileName("Save as: ")
        IF promptCancelled = 1 OR fileName = "" THEN
            CALL SetMsg("Save cancelled")
            EXIT SUB
        END IF
    END IF
    CALL SaveFile(fileName)
END SUB

' DoOpen -- Ctrl+O / F3: prompt for filename and load
SUB DoOpen
    DIM savedName AS STRING
    IF modified = 1 THEN
        CALL SetMsg("Unsaved changes! Save first (^S), or use ^N for New.")
        EXIT SUB
    END IF
    savedName = fileName
    CALL PromptFileName("Open file: ")
    ' PromptFileName stores the entered name in the global fileName
    IF fileName = "" OR fileName = savedName THEN
        fileName = savedName
        CALL SetMsg("Open cancelled")
        EXIT SUB
    END IF
    CALL LoadFile(fileName)
    CALL DrawAll
END SUB

' DoNew -- Ctrl+N / F4: clear buffer for a new file
SUB DoNew
    IF modified = 1 THEN
        CALL SetMsg("Unsaved changes! Press ^N again to discard, or ^S to save.")
        modified = 2   ' armed state: next ^N will discard
        EXIT SUB
    END IF
    ' Reset armed state and clear buffer
    modified = 0
    CALL InitBuffer
    fileName = "untitled.bas"
    CALL DrawAll
    CALL SetMsg("New file")
END SUB

' PromptFileName -- display an input prompt on the message bar
'   Result is stored in the global fileName
SUB PromptFileName(prompt AS STRING)
    DIM s     AS STRING
    DIM k     AS STRING
    DIM code  AS INTEGER

    promptCancelled = 0
    s = ""
    CALL SetMsg(prompt + s)

    DO
        k = ""
        DO WHILE LEN(k) = 0
            k = INKEY$
        LOOP

        IF LEN(k) = 1 THEN
            code = ASC(k)
            SELECT CASE code
                CASE 13     ' ENTER
                    EXIT DO
                CASE 27     ' ESC
                    promptCancelled = 1
                    s = ""
                    EXIT DO
                CASE 8      ' BACKSPACE
                    IF LEN(s) > 0 THEN
                        s = LEFT$(s, LEN(s) - 1)
                    END IF
                CASE ELSE
                    IF code >= 32 AND code <= 126 THEN
                        s = s + k
                    END IF
            END SELECT
        END IF
        CALL SetMsg(prompt + s)
    LOOP

    IF LEN(s) > 0 THEN
        fileName = s
    END IF
    msgText = ""
END SUB

' SaveFile -- write buffer to disk
SUB SaveFile(path AS STRING)
    DIM fnum AS INTEGER
    DIM i    AS INTEGER

    fnum = 1
    OPEN path FOR OUTPUT AS #fnum
    FOR i = 1 TO lineCount
        PRINT #fnum, lines(i)
    NEXT i
    CLOSE #fnum

    modified = 0
    CALL DrawTitleBar
    CALL SetMsg("Saved: " + path)
END SUB

' LoadFile -- read file into buffer
SUB LoadFile(path AS STRING)
    DIM fnum AS INTEGER
    DIM s    AS STRING
    DIM n    AS INTEGER

    ' Check file exists using SHELL$
    DIM check AS STRING
    check = TRIM$(SHELL$("test -f """ + path + """ && echo yes"))
    IF check <> "yes" THEN
        CALL SetMsg("File not found: " + path)
        EXIT SUB
    END IF

    CALL InitBuffer
    lineCount = 0

    fnum = 1
    OPEN path FOR INPUT AS #fnum
    DO WHILE NOT EOF(fnum)
        LINE INPUT #fnum, s
        ' Truncate very long lines
        IF LEN(s) > MAX_LINE_LEN THEN
            s = LEFT$(s, MAX_LINE_LEN)
        END IF
        lineCount = lineCount + 1
        lines(lineCount) = s
        IF lineCount >= MAX_LINES THEN EXIT DO
    LOOP
    CLOSE #fnum

    IF lineCount = 0 THEN
        lineCount = 1
        lines(1) = ""
    END IF

    curRow   = 1
    curCol   = 1
    topLine  = 1
    modified = 0
    CALL SetMsg("Loaded: " + path + " (" + TRIM$(STR$(lineCount)) + " lines)")
END SUB

' ==================================================================
' Compile and Run
' ==================================================================

' DoCompileOnly -- F6: compile and show errors
SUB DoCompileOnly
    DIM rc AS INTEGER

    ' Must save first
    IF modified = 1 OR fileName = "untitled.bas" THEN
        CALL SaveFile(fileName)
    END IF

    CALL SetMsg("Compiling " + fileName + " ...")

    ' Run bcc; redirect stderr to /tmp/ide_err for ShowErrors to read
    SHELL "PATH=$PATH:/usr/local/pa11_32/bin:.:.. bcc """ + fileName + """ -o /tmp/ide_bin > /tmp/ide_out 2>/tmp/ide_err"
    rc = ERRLEVEL

    IF rc = 0 THEN
        CALL SetMsg("Compiled OK: " + fileName)
    ELSE
        CALL ShowErrors
    END IF
END SUB

' DoCompileRun -- F5: compile and run
SUB DoCompileRun
    DIM rc AS INTEGER

    ' Must save first
    IF modified = 1 OR fileName = "untitled.bas" THEN
        CALL SaveFile(fileName)
    END IF

    CALL SetMsg("Compiling " + fileName + " ...")

    SHELL "PATH=$PATH:/usr/local/pa11_32/bin:.:.. bcc """ + fileName + """ -o /tmp/ide_bin > /tmp/ide_out 2>/tmp/ide_err"
    rc = ERRLEVEL

    IF rc <> 0 THEN
        CALL ShowErrors
        EXIT SUB
    END IF

    ' Run the compiled binary
    CLS
    LOCATE 1, 1
    COLOR COL_EDIT_FG
    PRINT "--- Running " + fileName + " ---"
    PRINT ""
    SHELL "/tmp/ide_bin"
    PRINT ""
    PRINT "--- Press any key to return to IDE ---"
    DIM k AS STRING
    k = ""
    DO WHILE LEN(k) = 0
        k = INKEY$
    LOOP

    ' Restore IDE screen
    CALL DrawAll
END SUB

' ShowErrors -- display compile error list (reads /tmp/ide_err)
SUB ShowErrors
    DIM errLine  AS STRING
    DIM allErr   AS STRING
    DIM fnum     AS INTEGER
    DIM n        AS INTEGER
    DIM i        AS INTEGER

    ' Read up to 10 error lines
    DIM errLines(10) AS STRING   ' up to 10 error lines
    n = 0

    fnum = 2
    OPEN "/tmp/ide_err" FOR INPUT AS #fnum
    DO WHILE NOT EOF(fnum) AND n < 10
        LINE INPUT #fnum, errLine
        n = n + 1
        errLines(n) = errLine
    LOOP
    CLOSE #fnum

    IF n = 0 THEN
        CALL SetMsg("Compile failed (no error output)")
        EXIT SUB
    END IF

    ' Show errors overlaid on the bottom of the screen
    CLS
    CALL DrawTitleBar
    CALL DrawEditArea

    ' Draw error panel over bottom of the screen
    DIM errTop AS INTEGER
    errTop = MSG_ROW - n
    IF errTop < EDIT_TOP THEN errTop = EDIT_TOP
    COLOR 12   ' bright red
    LOCATE errTop, 1
    PRINT SPACE$(SCREEN_COLS);
    LOCATE errTop, 1
    PRINT " Compile Errors (" + TRIM$(STR$(n)) + "):";

    FOR i = 1 TO n
        IF errTop + i < HELP_ROW THEN
            LOCATE errTop + i, 1
            DIM eline AS STRING
            eline = " " + errLines(i)
            IF LEN(eline) > SCREEN_COLS THEN
                eline = LEFT$(eline, SCREEN_COLS)
            ELSE
                eline = eline + SPACE$(SCREEN_COLS - LEN(eline))
            END IF
            PRINT eline;
        END IF
    NEXT i

    COLOR COL_HELP_FG
    LOCATE HELP_ROW, 1
    DIM helpStr AS STRING
    helpStr = " Press any key to dismiss..."
    helpStr = helpStr + SPACE$(SCREEN_COLS - LEN(helpStr))
    PRINT helpStr;

    COLOR COL_EDIT_FG
    LOCATE errTop, 1

    ' Wait for keypress
    DIM k AS STRING
    k = ""
    DO WHILE LEN(k) = 0
        k = INKEY$
    LOOP

    ' Jump cursor to first error line if parseable
    CALL TryJumpToError(errLines(1))

    CALL DrawAll
END SUB

' TryJumpToError -- parse "file:line:col: error..." and jump cursor
SUB TryJumpToError(errLine AS STRING)
    DIM colon1 AS INTEGER
    DIM colon2 AS INTEGER
    DIM lineNum AS INTEGER
    DIM s       AS STRING

    ' Format: filename:line:col: message
    colon1 = INSTR(errLine, ":")
    IF colon1 = 0 THEN EXIT SUB
    s = MID$(errLine, colon1 + 1)
    colon2 = INSTR(s, ":")
    IF colon2 = 0 THEN EXIT SUB
    s = LEFT$(s, colon2 - 1)
    lineNum = INT(VAL(s))
    IF lineNum < 1 THEN EXIT SUB
    IF lineNum > lineCount THEN lineNum = lineCount

    curRow = lineNum
    curCol = 1
    topLine = curRow - (EDIT_ROWS \ 2)
    IF topLine < 1 THEN topLine = 1
    CALL SetMsg("Error at line " + TRIM$(STR$(lineNum)))
END SUB

' ==================================================================
' DoQuit -- Ctrl+Q / Ctrl+X / F10
' ==================================================================
SUB DoQuit
    IF modified = 1 THEN
        CALL SetMsg("Unsaved changes! Press ^Q again to quit without saving.")
        modified = 3   ' armed: next ^Q/F10 will quit for real
        EXIT SUB
    END IF
    IF modified = 3 THEN
        ' Second press: quit without saving
        running = 0
        EXIT SUB
    END IF
    running = 0
END SUB
