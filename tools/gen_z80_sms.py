#!/usr/bin/env python3
"""SMS-boot payload generator, v4: the two-processor duet.

Save-state evidence, three rounds of it: the 68K's VDP REGISTER writes land
in mode 4 but its DATA-port writes never do; the Z80's DATA writes land but
its CONTROL writes duplicate into garbage. So each does what it can prove:
the 68K performs mode-4 entry and sets every VDP address; the Z80 streams
every payload byte through $7F00. A mailbox in Z80 RAM coordinates:

  $1FF8 command: 0 = idle, N = stream section N (1-based)
  $1FF9 done:    Z80 sets 1 after finishing a section
  $1F00..$1F03   idle heartbeat (ripple counter), state-forensics life-sign

Sections: 1 = CRAM (32B), 2 = tiles, 3..N = nametable runs pre-expanded to
[idx, 0] byte pairs so the Z80 streams blindly. The 68K writes the matching
VDP address word before each command (its ctrl-word writes are proven by
the mode-4 flip itself), pokes the command via a brief bus request, then
polls done the same way.
"""
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
STATE = ROOT / "rom" / "sms-diag.bs1"
OUT = ROOT / "md_src" / "z80_sms_hello.h"

VRAM_OFF = 40960

data = STATE.read_bytes()

def unswap(buf):
    out = bytearray(len(buf))
    out[0::2] = buf[1::2]
    out[1::2] = buf[0::2]
    return bytes(out)

vram = unswap(data[VRAM_OFF:VRAM_OFF + 0x4000])

runs = []
max_idx = 0
for row in range(28):
    ents = vram[0x3800 + row*64 : 0x3800 + row*64 + 64]
    idxs = [ents[i] for i in range(0, 64, 2)]
    used = [c for c, t in enumerate(idxs) if t]
    if not used:
        continue
    c0, c1 = used[0], used[-1]
    seq = idxs[c0:c1+1]
    max_idx = max(max_idx, max(seq))
    runs.append((0x3800 + row*64 + c0*2, seq))

tiles = vram[0 : (max_idx + 1) * 32]
cram = bytes([0x00] + [0x3F]*15 + [0x00]*16)
print(f"tiles 0..{max_idx} ({len(tiles)}B), {len(runs)} runs")

# ---- section data: 1=CRAM, 2=tiles, 3.. = runs as [idx,0] pairs ---------
sections = [cram, tiles] + [bytes(b for i in seq for b in (i, 0))
                            for _, seq in runs]
sect_cmds = [0xC000, 0x4000] + [0x4000 | addr for addr, _ in runs]

MAILBOX_CMD, MAILBOX_DONE = 0x1FF8, 0x1FF9

prog = bytearray()
labels = {}
fix16 = []
def emit(*bs): prog.extend(bs)
def label(n): labels[n] = len(prog)
def ref16(n): fix16.append((len(prog), n)); emit(0, 0)

emit(0xF3)                          # di
emit(0x31, 0xF0, 0x1F)              # ld sp,$1FF0
# FIRST ACT: silence the mailbox. It lives OUTSIDE the uploaded payload,
# and uninitialized Z80 RAM boots 0xFF — the Z80 read command 255,
# indexed garbage, and fired one blind data-port burst that walked the
# VDP cursor across VRAM and executed the font (the grey-menus killer).
emit(0xAF)                          # xor a
emit(0x32, MAILBOX_CMD & 0xFF, MAILBOX_CMD >> 8)
emit(0x32, MAILBOX_DONE & 0xFF, MAILBOX_DONE >> 8)
label("idle")
emit(0x21, 0x00, 0x1F)              # ld hl,$1F00 (heartbeat)
emit(0x34)                          # inc (hl)
emit(0x3A, MAILBOX_CMD & 0xFF, MAILBOX_CMD >> 8)   # ld a,(cmd)
emit(0xB7)                          # or a
emit(0x28, (labels["idle"] - (len(prog) + 2)) & 0xFF)  # jr z,idle
# A = section 1..N -> table entry at sect_tab + (A-1)*4 : [ptr16, len16]
emit(0x3D)                          # dec a
emit(0x87, 0x87)                    # add a,a / add a,a  (x4)
emit(0x26, 0x00, 0x6F)              # ld h,0 / ld l,a
emit(0x11); ref16("sect_tab")       # ld de,sect_tab
emit(0x19)                          # add hl,de
emit(0x5E, 0x23, 0x56, 0x23)        # ld e,(hl)/inc/ld d,(hl)/inc  -> DE=ptr
emit(0x4E, 0x23, 0x46)              # ld c,(hl)/inc/ld b,(hl)      -> BC=len
emit(0xEB)                          # ex de,hl  (HL=ptr, DE=scratch)
label("strm")
emit(0x7E)                          # ld a,(hl)
emit(0x32, 0x00, 0x7F)              # ld ($7F00),a — the proven path
emit(0x23, 0x0B, 0x78, 0xB1)        # inc hl / dec bc / ld a,b / or c
emit(0x20, (labels["strm"] - (len(prog) + 2)) & 0xFF)  # jr nz,strm
emit(0xAF)                          # xor a
emit(0x32, MAILBOX_CMD & 0xFF, MAILBOX_CMD >> 8)   # cmd = 0
emit(0x3C)                          # inc a  -> 1
emit(0x32, MAILBOX_DONE & 0xFF, MAILBOX_DONE >> 8) # done = 1
emit(0x18, (labels["idle"] - (len(prog) + 2)) & 0xFF)  # jr idle

label("sect_tab")
tab_pos = len(prog)
for _ in sections: emit(0, 0, 0, 0)
sect_offs = []
for sdata in sections:
    label(f"s{len(sect_offs)}")
    sect_offs.append(len(prog))
    prog.extend(sdata)

for i, (off, ln) in enumerate(zip(sect_offs, (len(x) for x in sections))):
    e = tab_pos + i*4
    prog[e] = off & 0xFF; prog[e+1] = off >> 8
    prog[e+2] = ln & 0xFF; prog[e+3] = ln >> 8
for off, name in fix16:
    prog[off] = labels[name] & 0xFF
    prog[off+1] = labels[name] >> 8

if len(prog) & 1:
    prog.append(0)
assert len(prog) <= 0x1F00, f"{len(prog)}B overflows Z80 RAM"

# ---- mode-5 text blob (v6): the diag screen as GAME-FONT tile words ----
# Ares' MD VDP never implemented mode 4 (debug "unimplemented M5=0"), so
# the visible path is the mode-5 text layer. Map the diag's ASCII to the
# boot font's tile ids (mars.c NextChr mapping) and emit rows the 68K can
# stream straight into Name Table B.
def font_tile(ch):
    if '0' <= ch <= '9': return ord(ch) - ord('0') + 2
    if 'A' <= ch <= 'Z': return ord(ch) - ord('A') + 12
    m = {':':38, '.':39, '-':40, '>':41, '|':42, '+':43, '%':44, ' ':0}
    return m.get(ch, 1)
text = bytearray()
for addr, seq in runs:
    row = (addr - 0x3800) // 64
    col = ((addr - 0x3800) % 64) // 2
    chars = [chr(i + 32) if 0 < i < 95 else ' ' for i in seq]
    text += bytes([row, col + 4, len(seq)])      # +4: center 32 cols in 40
    text += bytes(font_tile(c) for c in chars)
text.append(0xFF)                                # terminator (row 255)
cmds = ", ".join(f"0x{c:04X}" for c in sect_cmds)
offs = ", ".join(str(o) for o in sect_offs)
lens = ", ".join(str(len(x)) for x in sections)
lines = [", ".join(f"0x{b:02X}" for b in prog[i:i+12]) for i in range(0, len(prog), 12)]
OUT.write_text(
    "/* Auto-generated by tools/gen_z80_sms.py — do not edit. */\n"
    "#ifndef Z80_SMS_HELLO_H\n#define Z80_SMS_HELLO_H\n"
    f"#define Z80_SMS_HELLO_LEN {len(prog)}\n"
    f"#define Z80_SMS_SECT_COUNT {len(sections)}\n"
    f"#define Z80_SMS_MAILBOX_CMD 0x{MAILBOX_CMD:04X}\n"
    f"#define Z80_SMS_MAILBOX_DONE 0x{MAILBOX_DONE:04X}\n"
    f"#define Z80_SMS_TEXT_LEN {len(text)}\n"
    "static const unsigned char z80_sms_text[] = {\n    "
    + ",\n    ".join(", ".join(f"0x{b:02X}" for b in text[i:i+12])
                      for i in range(0, len(text), 12))
    + "\n};\n"
    f"static const unsigned short z80_sms_sect_cmd[] = {{ {cmds} }};\n"
    f"static const unsigned short z80_sms_sect_off[] = {{ {offs} }};\n"
    f"static const unsigned short z80_sms_sect_len[] = {{ {lens} }};\n"
    "static const unsigned char z80_sms_hello[] = {\n    "
    + ",\n    ".join(lines)
    + "\n};\n#endif\n")
print(f"wrote {OUT}: {len(prog)}B, {len(sections)} sections")
