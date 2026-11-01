# Makefile for bcc-ide
#
# Builds the IDE binary by compiling ide.bas with bcc.
# bcc and libbasic.a must already be built in the parent directory.
#
# Usage (from this directory):
#   make              -- build bcc-ide
#   make install      -- install bcc-ide to $(PREFIX)/bin
#   make uninstall    -- remove bcc-ide from $(PREFIX)/bin
#   make clean        -- remove bcc-ide binary
#
# PREFIX defaults to /usr/local (same default as the root Makefile).
# Override with:   make PREFIX=/opt/local install

PREFIX   ?= /usr/local
BCC      ?= ../bcc
IDEBIN    = bcc-ide
IDESRC    = ide.bas

# ------------------------------------------------------------------
.PHONY: all install uninstall clean

all: $(IDEBIN)

$(IDEBIN): $(IDESRC) $(BCC) ../libbasic.a
	$(BCC) $(IDESRC) -o $(IDEBIN)

# ------------------------------------------------------------------
# Install
# ------------------------------------------------------------------
install: $(IDEBIN)
	mkdir -p $(PREFIX)/bin
	cp $(IDEBIN) $(PREFIX)/bin/$(IDEBIN)
	chmod 755   $(PREFIX)/bin/$(IDEBIN)
	@echo "Installed $(IDEBIN) to $(PREFIX)/bin"
	@echo "Launch with:  bcc-ide [file.bas]"

# ------------------------------------------------------------------
# Uninstall
# ------------------------------------------------------------------
uninstall:
	rm -f $(PREFIX)/bin/$(IDEBIN)
	@echo "Removed $(PREFIX)/bin/$(IDEBIN)"

# ------------------------------------------------------------------
# Clean
# ------------------------------------------------------------------
clean:
	rm -f $(IDEBIN)
