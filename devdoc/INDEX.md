# devdoc — reference library

Offline hardware/programming references for the SMS-on-32X work: Z80 CPU, SMS video, Yamaha FM chips, plus hardware-mod schematics and a few off-topic keepsakes. Untracked by git.

## QUICK ANSWERS

| Need | File |
|---|---|
| Opcode lookup (hex → mnemonic; base/CB/ED pages) | `z80/z80-opcode-grid.txt` |
| T-states per instruction, sorted by opcode | `z80/z80-docs/z80-inst-list.txt` |
| Authoritative instruction detail (searchable PDF) | `z80/z80cpu_um.pdf` (ZiLOG CPU User Manual) |
| VDP registers + scrolling (regs 0-10, name table, SAT, timing) | `sms/msvdp-charles-macdonald.txt` |
| SMS memory map, I/O ports, pad reading | `sms/smstech-charles-macdonald.txt` |
| FM chip (YM2413) registers/operation | `audio/` |

## Directories

- `z80/` — opcode grid, flag-affection table, ZiLOG official CPU User Manual (PDF), `z80-docs/` (15-file pack: instruction lists/tables, complete specs .doc, syntax/flags/interrupts/undocumented-opcodes/FAQ htm; two ZX Spectrum files ride along), `books/` (three scanned Z80 books: Zaks, Leventhal & Saville, Barden).
- `sms/` — Master System schematic GIF plus the three smspower.org technical docs (MacDonald VDP doc, MacDonald hardware notes, Talbot-Watkins technical summary).
- `audio/` — YM2413 application manual (31 GIF page scans in `ym2413-application-manual/`) and `ym2413-datasheet.pdf`.
- `hardware-mods/` — `smsopll/` YM2413-retrofit schematics + mails.txt, SMS gender-adapter pinout PNG, `GGFix.doc` (Game Gear capacitor repair).
- `offtopic/` — NEC uPD765A floppy controller PDF, 93C46 EEPROM datasheet (zipped), SC-3000 tape-format scan JPEGs, Crawford's *The Art of Computer Game Design* (fine reading, zero engineering content).
- `_redundant-safe-to-delete/` — only verified duplicates and shipping zips whose extracted contents now live in the tree, plus stray .DS_Store files. `Z80-Info.rtf` alone is 108MB and is byte-identical in text to `z80/z80-docs/z80-complete-specs.doc`. Safe to `rm -rf` to reclaim ~110MB+.

## KNOWN GAPS

- No SN76489 PSG doc in the library — the smspower.org SN76489 page is the reference.
- All three SMS downloads succeeded 2026-08-14; nothing pending.

## Best in-tree references OUTSIDE devdoc

- `srcref/sms/bios13.asm` — commented BIOS disassembly; the Snail Maze code demonstrates reg-8 scrolling + seam column streaming.
- `srcref/devkitSMS/SMSlib` — C API with register-level VDP idioms.
