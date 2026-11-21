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
if [ ! -f "$target/src/main.c" ]; then echo "Missing src/main.c" >&2; exit 1; fi
got=$(cksum < "$target/src/main.c" | awk '{print $1 " " $2}')
if [ "$got" != "2300001829 37172" ]; then echo "Mismatch: src/main.c; require unmodified onekeyirc 0.1.35 English source r1" >&2; exit 1; fi
if [ ! -f "$target/src/idle_commands.h" ]; then echo "Missing src/idle_commands.h" >&2; exit 1; fi
got=$(cksum < "$target/src/idle_commands.h" | awk '{print $1 " " $2}')
if [ "$got" != "2370137536 2141" ]; then echo "Mismatch: src/idle_commands.h; require unmodified onekeyirc 0.1.35 English source r1" >&2; exit 1; fi
if [ ! -f "$target/configure" ]; then echo "Missing configure" >&2; exit 1; fi
got=$(cksum < "$target/configure" | awk '{print $1 " " $2}')
if [ "$got" != "3086782442 4582" ]; then echo "Mismatch: configure; require unmodified onekeyirc 0.1.35 English source r1" >&2; exit 1; fi
if [ ! -f "$target/src/editor.c" ]; then echo "Missing src/editor.c" >&2; exit 1; fi
got=$(cksum < "$target/src/editor.c" | awk '{print $1 " " $2}')
if [ "$got" != "3922425783 11099" ]; then echo "Mismatch: src/editor.c; require unmodified onekeyirc 0.1.35 English source r1" >&2; exit 1; fi
if [ ! -f "$target/src/editor.h" ]; then echo "Missing src/editor.h" >&2; exit 1; fi
got=$(cksum < "$target/src/editor.h" | awk '{print $1 " " $2}')
if [ "$got" != "290667678 2781" ]; then echo "Mismatch: src/editor.h; require unmodified onekeyirc 0.1.35 English source r1" >&2; exit 1; fi
if [ ! -f "$target/src/nicks.c" ]; then echo "Missing src/nicks.c" >&2; exit 1; fi
got=$(cksum < "$target/src/nicks.c" | awk '{print $1 " " $2}')
if [ "$got" != "1013396401 5910" ]; then echo "Mismatch: src/nicks.c; require unmodified onekeyirc 0.1.35 English source r1" >&2; exit 1; fi
if [ ! -f "$target/tests/test_nicks.c" ]; then echo "Missing tests/test_nicks.c" >&2; exit 1; fi
got=$(cksum < "$target/tests/test_nicks.c" | awk '{print $1 " " $2}')
if [ "$got" != "509016199 7672" ]; then echo "Mismatch: tests/test_nicks.c; require unmodified onekeyirc 0.1.35 English source r1" >&2; exit 1; fi
(cd "$dir/overlay" && tar -cf - .) | (cd "$target" && tar -xf -)
echo "Optional language+RTL patch r14 applied; configure and compile now."
