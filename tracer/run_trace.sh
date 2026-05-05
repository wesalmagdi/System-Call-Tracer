#!/bin/bash
#
# run_trace.sh
# Boots xv6, lets you run any strace commands, saves JSON to log file.
#
# Usage:  ./run_trace.sh
# Quit:   Ctrl-A then X

# self-fix: remove Windows carriage returns if file was copied from Windows
sed -i 's/\r//g' "$0" 2>/dev/null || true

# --- change this path to match your backend folder ---
OUTFILE="../backend/Data/xv6_trace.log"
# -----------------------------------------------------

TMPLOG="/tmp/xv6_session.log"
mkdir -p "$(dirname "$OUTFILE")"

echo "==> Booting xv6 — keyboard is fully yours"
echo "==> Run your strace commands, quit with Ctrl-A X"
echo ""

# script records the full terminal session while keeping keyboard interactive
script -q -c "make qemu" "$TMPLOG"

# strip ANSI color codes then keep only JSON lines (lines starting with {)
sed 's/\x1b\[[0-9;]*[a-zA-Z]//g' "$TMPLOG" | grep '^{' > "$OUTFILE"

echo ""
echo "==> Done. $(wc -l < "$OUTFILE") events saved to $OUTFILE"
