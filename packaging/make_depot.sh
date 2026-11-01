#!/bin/sh
# make_depot.sh -- Generate SD-UX PSF for the configured PREFIX and run swpackage
#
# Usage:
#   sh packaging/make_depot.sh <PREFIX> <VERSION> <DEPOT_FILE>

set -e

PREFIX="${1:-/usr/local/pa11_32}"
VERSION="${2:-1.0}"
DEPOT_OUT="${3:-hpux-bc-${VERSION}.depot}"
PSF_TMP="packaging/hpux-bc.psf.gen"

SWPACKAGE="/usr/sbin/swpackage"
if [ ! -x "$SWPACKAGE" ]; then
    echo "make_depot.sh: /usr/sbin/swpackage not found (must be run on HP-UX)" >&2
    exit 1
fi

chmod 755 packaging/checkinstall

cat > "$PSF_TMP" << EOF
depot
  layout_version 1.0

vendor
  tag            ScottJ
  title          HP-UX BASIC Compiler Project
  description    HP-UX BASIC Compiler (bcc), Runtime Library, and Full-Screen IDE
end

product
  tag            HPUX-BC
  revision       ${VERSION}
  architecture   HP-UX_B.11.11_32/64
  vendor_tag     ScottJ
  title          HP-UX BASIC Compiler and IDE
  description    QBasic/QuickBASIC-compatible BASIC compiler (bcc), C89 runtime library (libbasic.a), and terminal IDE (bcc-ide) for HP-UX 11i PA-RISC.
  os_name        HP-UX
  os_release     ?.11.*
  os_version     *
  machine_type   9000/[678]*
  directory      ${PREFIX}
  is_locatable   true
  checkinstall   packaging/checkinstall

  fileset
    tag            BCC-RUN
    title          HP-UX BASIC Compiler (bcc) and Runtime Library (libbasic.a)
    revision       ${VERSION}
    description    Compiler driver (bcc), static runtime library (libbasic.a), and C header (basic_runtime.h)
    prerequisites  gcc
    prerequisites  ncurses

    file -m 0755 -o root -g sys bcc                     ${PREFIX}/bin/bcc
    file -m 0644 -o root -g sys libbasic.a              ${PREFIX}/lib/libbasic.a
    file -m 0644 -o root -g sys runtime/basic_runtime.h ${PREFIX}/include/basic_runtime.h
  end

  fileset
    tag            BCC-IDE
    title          HP-UX BASIC Integrated Development Environment (bcc-ide)
    revision       ${VERSION}
    description    Full-screen terminal BASIC IDE and source code
    prerequisites  HPUX-BC.BCC-RUN
    prerequisites  ncurses

    file -m 0755 -o root -g sys ide/bcc-ide             ${PREFIX}/bin/bcc-ide
    file -m 0644 -o root -g sys ide/ide.bas             ${PREFIX}/share/hpux-bc/ide.bas
  end

  fileset
    tag            BCC-DOC
    title          HP-UX BASIC Reference Manual
    revision       ${VERSION}
    description    Language Reference and Documentation for HPUX-BC

    file -m 0644 -o root -g sys REFERENCE.md            ${PREFIX}/share/doc/hpux-bc/REFERENCE.md
  end
end
EOF

rm -f "$DEPOT_OUT"
echo "Building SD-UX tape depot: $DEPOT_OUT (PREFIX=$PREFIX) ..."
"$SWPACKAGE" -s "$PSF_TMP" -x media_type=tape @ "$(pwd)/$DEPOT_OUT"
rm -f "$PSF_TMP"

echo ""
echo "Created depot: $(pwd)/$DEPOT_OUT"
echo "Verify contents and dependencies with:"
echo "  /usr/sbin/swlist -d -l fileset -a prerequisites @ $(pwd)/$DEPOT_OUT"
echo "Install with:"
echo "  /usr/sbin/swinstall -s $(pwd)/$DEPOT_OUT HPUX-BC"

