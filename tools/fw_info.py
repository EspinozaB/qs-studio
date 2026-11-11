#!/usr/bin/env python3
"""Identify a QS operating system ROM dump (the H8/510 program ROM) without changing it.

Usage: python3 tools/fw_info.py <file.bin> [...]

Prints the model and version string the OS shows at power-up, the byte order of the dump, the reset vector and the
byte-sum checksum (the QS shows the same 4-digit checksum, e.g. QS6 2.00 = F010). QS Studio never ships these files.
"""
import re, struct, sys

for path in sys.argv[1:]:
    d = open(path, "rb").read()
    swapped = bytes(b for pair in zip(d[1::2], d[0::2]) for b in pair)
    # The H8/510 is big-endian; most dumps are stored with each 16-bit word byte-swapped.
    for label, img in (("as stored", d), ("byte-swapped", swapped)):
        m = re.search(rb"(QS[0-9.]+R?)\s+V(\d\.\d\d)\s+([A-Za-z]+\.? [0-9a-z]+,? \d{4})", img)
        if m:
            reset = struct.unpack(">I", img[0:4])[0]
            print(f"{path}: {m.group(1).decode()} OS {m.group(2).decode()} ({m.group(3).decode()}), "
                  f"{len(d) // 1024} KB, words stored {'byte-swapped' if label == 'byte-swapped' else 'big-endian'}, "
                  f"reset vector 0x{reset:06X}, byte sum {sum(d) & 0xFFFF:04X}")
            break
    else:
        print(f"{path}: no QS version string found ({len(d)} bytes, byte sum {sum(d) & 0xFFFF:04X})")
