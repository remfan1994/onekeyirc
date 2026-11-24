#!/bin/sh
set -eu
if [ "$#" -ne 1 ]; then
    echo "usage: sh apply.sh SOURCE_TREE" >&2
    exit 2
fi
target=$1
dir=$(CDPATH= cd "$(dirname "$0")" && pwd)
if [ ! -d "$target" ] || [ -L "$target" ] || [ ! -d "$dir/overlay" ]; then
    echo "A regular source directory and a valid overlay are required" >&2
    exit 1
fi
if [ -e "$target/LANGUAGE-PACK-REVISION.txt" ] || [ -e "$target/Makefile" ]; then
    echo "Already patched or configured: use fresh English source r1" >&2
    exit 1
fi
if [ ! -f "$target/SOURCE-REVISION.txt" ] ||
   ! grep -q 'APPLICATION VERSION: 0.1.36' "$target/SOURCE-REVISION.txt" ||
   ! grep -q 'ENGLISH SOURCE REVISION: r1' "$target/SOURCE-REVISION.txt"; then
    echo "The source revision is not onekeyirc 0.1.36 English r1" >&2
    exit 1
fi
# Never extract an overlay through a source-tree symlink or an overlay link:
# tar may otherwise write to a destination outside SOURCE_TREE.
if [ -n "$(cd "$target" && find . -type l -print)" ] ||
   [ -n "$(cd "$dir/overlay" && find . -type l -print)" ]; then
    echo "Symlink found; refusing an unsafe overlay extraction" >&2
    exit 1
fi
if command -v sha256sum >/dev/null 2>&1; then
    file_sha256() { sha256sum "$1" | awk '{print $1}'; }
elif command -v sha256 >/dev/null 2>&1; then
    # OpenBSD base-system SHA-256 utility.
    file_sha256() { sha256 -q "$1"; }
else
    echo "SHA-256 utility required (sha256sum or sha256)" >&2
    exit 1
fi
# Validate ALL existing files that the overlay would replace, not only C files.
# Refuse silently modified or wrong-version trees before touching any files.
while read -r expected rel; do
    [ -n "$expected" ] || continue
    if [ ! -f "$target/$rel" ] || [ -L "$target/$rel" ]; then
        echo "Missing or unsafe source: $rel" >&2
        exit 1
    fi
    actual=$(file_sha256 "$target/$rel")
    if [ "$actual" != "$expected" ]; then
        echo "Wrong or modified source: $rel" >&2
        exit 1
    fi
done < "$dir/BASE-SHA256SUMS"
# Both source files and generated tests are optional; the original English
# archive and its application version stay untouched.
(cd "$dir/overlay" && tar -cf - .) | (cd "$target" && tar -xf -)
echo "Optional language+RTL patch r85 applied to onekeyirc 0.1.36."
echo "Compile, then use --version to check [lang-rtl-r85]."
