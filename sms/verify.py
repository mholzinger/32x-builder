#!/usr/bin/env python3
"""Sanity-check an SMS ROM: size, TMR SEGA header, checksum, region/size code."""
import sys

path = sys.argv[1]
rom = open(path, "rb").read()

ok = True

if len(rom) != 32768:
    print(f"FAIL: size {len(rom)}, expected 32768")
    ok = False

magic = rom[0x7FF0:0x7FF8]
if magic != b"TMR SEGA":
    print(f"FAIL: header magic {magic!r}, expected b'TMR SEGA'")
    ok = False

expected = rom[0x7FFA] | (rom[0x7FFB] << 8)
computed = sum(rom[:0x7FF0]) & 0xFFFF
if expected != computed:
    print(f"FAIL: checksum header ${expected:04X} != computed ${computed:04X}")
    ok = False

rs = rom[0x7FFF]
region = {0x3: "SMS Japan", 0x4: "SMS Export", 0x5: "GG Japan",
          0x6: "GG Export", 0x7: "GG International"}.get(rs >> 4, "?")
size = {0xA: "8KB", 0xB: "16KB", 0xC: "32KB", 0xD: "48KB",
        0xE: "64KB", 0xF: "128KB", 0x0: "256KB"}.get(rs & 0xF, "?")

if ok:
    print(f"OK: {len(rom)} bytes, TMR SEGA, checksum ${computed:04X}, "
          f"region={region}, size={size}")
else:
    sys.exit(1)
