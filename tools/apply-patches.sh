#!/bin/bash
# Apply all XNU patches to natrium kernel sources
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
XNU_SRC="$SCRIPT_DIR/../xnu/src"
PATCHES="$SCRIPT_DIR/../xnu/patches"

cd "$XNU_SRC"
FAILED=0
for patch in $(ls "$PATCHES"/*.patch 2>/dev/null | sort); do
    name=$(basename "$patch")
    if patch -p1 --dry-run -s < "$patch" 2>/dev/null; then
        patch -p1 -s < "$patch"
        echo "✓ $name"
    else
        # Try with fuzz
        if patch -p1 -F3 --dry-run -s < "$patch" 2>/dev/null; then
            patch -p1 -F3 -s < "$patch"
            echo "~ $name (fuzz)"
        else
            echo "✗ $name FAILED"
            FAILED=$((FAILED+1))
        fi
    fi
done
echo ""
[ $FAILED -eq 0 ] && echo "All patches applied." || echo "$FAILED patches failed — review before building."
