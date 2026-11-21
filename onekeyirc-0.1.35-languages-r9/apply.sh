#!/bin/sh
set -eu
if [ "$#" -ne 1 ]; then echo "usage: sh apply.sh SOURCE_TREE" >&2; exit 2; fi
target=$1
dir=$(CDPATH= cd "$(dirname "$0")" && pwd)
if [ -e "$target/LANGUAGE-PACK-REVISION.txt" ]; then echo "A language pack is already present" >&2; exit 1; fi
if [ ! -f "$target/SOURCE-REVISION.txt" ]; then echo "Expected English source revision r1" >&2; exit 1; fi
if [ ! -f "$target/src/app.h" ]; then echo "Missing src/app.h" >&2; exit 1; fi
got=$(cksum < "$target/src/app.h" | awk '{print $1 " " $2}')
if [ "$got" != "1351534566 15011" ]; then echo "Mismatch: src/app.h; require unmodified onekeyirc 0.1.35 English source r1" >&2; exit 1; fi
if [ ! -f "$target/src/idle.c" ]; then echo "Missing src/idle.c" >&2; exit 1; fi
got=$(cksum < "$target/src/idle.c" | awk '{print $1 " " $2}')
if [ "$got" != "3296524390 175545" ]; then echo "Mismatch: src/idle.c; require unmodified onekeyirc 0.1.35 English source r1" >&2; exit 1; fi
if [ ! -f "$target/src/idle_commands.c" ]; then echo "Missing src/idle_commands.c" >&2; exit 1; fi
got=$(cksum < "$target/src/idle_commands.c" | awk '{print $1 " " $2}')
if [ "$got" != "576252023 157508" ]; then echo "Mismatch: src/idle_commands.c; require unmodified onekeyirc 0.1.35 English source r1" >&2; exit 1; fi
if [ ! -f "$target/Makefile.in" ]; then echo "Missing Makefile.in" >&2; exit 1; fi
got=$(cksum < "$target/Makefile.in" | awk '{print $1 " " $2}')
if [ "$got" != "1001833619 49852" ]; then echo "Mismatch: Makefile.in; require unmodified onekeyirc 0.1.35 English source r1" >&2; exit 1; fi
if [ ! -f "$target/src/l10n.h" ]; then echo "Missing src/l10n.h" >&2; exit 1; fi
got=$(cksum < "$target/src/l10n.h" | awk '{print $1 " " $2}')
if [ "$got" != "3301173330 261" ]; then echo "Mismatch: src/l10n.h; require unmodified onekeyirc 0.1.35 English source r1" >&2; exit 1; fi
(cd "$dir/overlay" && tar -cf - .) | (cd "$target" && tar -xf -)
echo "Language pack r9 applied; configure and compile now."
