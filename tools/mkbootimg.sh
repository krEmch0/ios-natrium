#!/bin/bash
# Build Android boot image for natrium (Mi 5s Plus)
# Layout: [2K header] [shim binary] [payload ramdisk]

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$SCRIPT_DIR/.."

SHIM="$ROOT/shim/out/shim.bin"
PAYLOAD="$ROOT/xnu/out/payload.bin"
OUTPUT="$ROOT/natrium-boot.img"
PAGE=4096

if [ ! -f "$SHIM" ]; then
    echo "ERROR: shim not built. Run: cd shim && make"
    exit 1
fi
if [ ! -f "$PAYLOAD" ]; then
    echo "ERROR: payload not assembled. Run: python3 tools/mkpayload.py ..."
    exit 1
fi

# Try mkbootimg (Android tools)
if command -v mkbootimg &>/dev/null; then
    mkbootimg \
        --kernel "$SHIM" \
        --ramdisk "$PAYLOAD" \
        --base 0x80000000 \
        --kernel_offset 0x00080000 \
        --ramdisk_offset 0x01000000 \
        --tags_offset 0x00000100 \
        --pagesize $PAGE \
        --cmdline "" \
        --output "$OUTPUT"
    echo "✓ Boot image: $OUTPUT ($(stat -c%s "$OUTPUT") bytes)"
else
    # Manual construction without mkbootimg
    python3 - "$SHIM" "$PAYLOAD" "$OUTPUT" "$PAGE" <<'PYEOF'
import sys, struct, os

shim_path, payload_path, out_path, page_size_str = sys.argv[1:]
page = int(page_size_str)

def read(path):
    with open(path, 'rb') as f: return f.read()

def pad(data, p):
    r = len(data) % p
    return data + b'\0' * (p - r) if r else data

shim    = pad(read(shim_path), page)
payload = pad(read(payload_path), page)

# Android boot image v0 header (608 bytes, padded to page)
MAGIC = b'ANDROID!'
kernel_size   = len(shim)
ramdisk_size  = len(payload)
kernel_addr   = 0x80080000
ramdisk_addr  = 0x82000000
second_addr   = 0x80f00000
tags_addr     = 0x80000100
cmdline       = b'\0' * 512
extra_cmdline = b'\0' * 1024
name          = b'\0' * 16

hdr = struct.pack('<8sIIIIIIII16s512sII8I1024s',
    MAGIC,
    kernel_size, kernel_addr,
    ramdisk_size, ramdisk_addr,
    0, second_addr,         # second stage: none
    tags_addr, page,
    0, 0,                   # unused
    name, cmdline,
    0, 0, 0, 0, 0, 0, 0, 0, # id[8]
    extra_cmdline,
)
hdr_padded = pad(hdr, page)

with open(out_path, 'wb') as f:
    f.write(hdr_padded)
    f.write(shim)
    f.write(payload)

print(f'✓ Boot image: {out_path} ({os.path.getsize(out_path)} bytes)')
PYEOF
fi
