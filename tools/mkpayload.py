#!/usr/bin/env python3
"""
Assemble XNU kernel payload for natrium boot shim.

Format:
  [payload_hdr]
  [blob_entry * num_blobs]
  [kernel Mach-O]
  [ADT binary]
  [bootargs string]
  [firmware blobs...]
"""

import struct
import sys
import os
import argparse

PAYLOAD_MAGIC = 0x444C5058  # 'XPLD'
PAYLOAD_VERSION = 1

def align_up(val, align):
    return (val + align - 1) & ~(align - 1)

def main():
    parser = argparse.ArgumentParser(description='Assemble natrium XNU payload')
    parser.add_argument('kernel', help='XNU kernel Mach-O')
    parser.add_argument('adt',    help='Apple Device Tree binary')
    parser.add_argument('output', help='Output payload.bin')
    parser.add_argument('--cmdline', default='', help='Kernel boot arguments')
    parser.add_argument('--blob', action='append', default=[],
                        metavar='NAME=PATH',
                        help='Firmware blob (e.g. GPU-PM4=firmware/gpu/pm4.fw)')
    args = parser.parse_args()

    blobs = []
    for b in args.blob:
        name, path = b.split('=', 1)
        if not os.path.exists(path):
            print(f'Warning: blob {name} not found at {path}, skipping')
            continue
        with open(path, 'rb') as f:
            data = f.read()
        blobs.append((name, data))
        print(f'  Blob {name}: {len(data)} bytes')

    with open(args.kernel, 'rb') as f:
        kernel_data = f.read()
    print(f'  Kernel: {len(kernel_data)} bytes')

    with open(args.adt, 'rb') as f:
        adt_data = f.read()
    print(f'  ADT: {len(adt_data)} bytes')

    bootargs = (args.cmdline + '\0').encode('utf-8')

    # Layout: header + blob_entries + kernel + adt + bootargs + blobs
    HDR_SIZE       = 10 * 4   # 10 uint32s
    BLOB_ENTRY_SIZE = 32 + 4 * 4  # name[32] + offset/size/addr_lo/addr_hi

    data_offset = HDR_SIZE + len(blobs) * BLOB_ENTRY_SIZE

    kernel_offset = data_offset
    kernel_size   = len(kernel_data)
    adt_offset    = kernel_offset + align_up(kernel_size, 64)
    adt_size      = len(adt_data)
    bootargs_offset = adt_offset + align_up(adt_size, 64)
    bootargs_size   = len(bootargs)

    blob_offset = bootargs_offset + align_up(bootargs_size, 64)
    blob_entries = []
    for name, data in blobs:
        blob_entries.append((name, blob_offset, len(data)))
        blob_offset += align_up(len(data), 64)

    total_size = blob_offset

    # Build header
    hdr = struct.pack('<IIIIIIIIII',
        PAYLOAD_MAGIC,
        PAYLOAD_VERSION,
        kernel_offset,
        kernel_size,
        adt_offset,
        adt_size,
        bootargs_offset,
        bootargs_size,
        len(blobs),
        total_size,
    )

    # Build blob entries
    blob_entry_data = b''
    for i, (name, data) in enumerate(blobs):
        entry_name, entry_off, entry_size = blob_entries[i][0], blob_entries[i][1], blob_entries[i][2]
        name_bytes = entry_name.encode('utf-8')[:31].ljust(32, b'\0')
        blob_entry_data += name_bytes + struct.pack('<IIII', entry_off, entry_size, 0, 0)

    # Assemble payload buffer
    buf = bytearray(total_size)

    def write_at(offset, data):
        buf[offset:offset+len(data)] = data

    write_at(0, hdr)
    write_at(HDR_SIZE, blob_entry_data)
    write_at(kernel_offset, kernel_data)
    write_at(adt_offset, adt_data)
    write_at(bootargs_offset, bootargs)
    for i, (name, data) in enumerate(blobs):
        write_at(blob_entries[i][1], data)

    with open(args.output, 'wb') as f:
        f.write(buf)

    print(f'\n✓ Payload written: {args.output} ({total_size} bytes)')

if __name__ == '__main__':
    main()
