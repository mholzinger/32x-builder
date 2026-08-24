; =====================================================================
; SMS DIAGNOSTIC ROM - HELLO WORLD
; Proves a Sega Master System (or SMS core/emulator) boots and runs.
;
; What it exercises / reports on screen:
;   - Z80 fetch/exec from ROM, stack in work RAM
;   - VDP mode 4: register init, VRAM clear, CRAM, tiles, tilemap
;   - ROM bus integrity: 16-bit checksum of $0000-$7FEF vs header word
;   - Region detect (export vs japanese) via TH line readback on $3F/$DD
;   - NTSC/PAL detect by counting scanlines (vcounter changes) per frame
;   - Frame IRQ (IM 1, $0038): live FRAMES counter
;   - PAUSE button NMI ($0066): live PAUSE counter
;   - Controller port $DC: live pad 1 state (UDLR12)
;
; Build: make   (wla-z80 + wlalink, font from gen_font.py)
; =====================================================================

.MEMORYMAP
DEFAULTSLOT 0
SLOTSIZE $8000
SLOT 0 $0000
.ENDME

.ROMBANKMAP
BANKSTOTAL 1
BANKSIZE $8000
BANKS 1
.ENDRO

.EMPTYFILL $00

; wla-dx writes "TMR SEGA" + checksum at $7FF0 (export BIOS validates this)
.SMSHEADER
   PRODUCTCODE 26, 70, 2
   VERSION 0
   REGIONCODE 4        ; SMS export
   RESERVEDSPACE $FF, $FF
   ROMSIZE $C          ; 32KB -> checksum range $0000-$7FEF
.ENDSMS

; ---------------- hardware ports ----------------
.DEFINE VDP_DATA  $BE
.DEFINE VDP_CTRL  $BF
.DEFINE IO_CTRL   $3F
.DEFINE IO_DC     $DC   ; pad 1: bits 0-5 = U D L R 1 2, active low
.DEFINE IO_DD     $DD   ; pad 2 / TH readback
.DEFINE VCOUNT    $7E   ; VDP vertical counter

; ---------------- VRAM layout ----------------
; tiles at $0000 (tile n = ASCII n+32), name table at $3800
.DEFINE NT_BASE   $3800

; name-table addresses of the dynamic fields (row*64 + col*2)
.DEFINE CKVAL_NT  NT_BASE + 5*64 + 12*2
.DEFINE CKST_NT   NT_BASE + 5*64 + 19*2
.DEFINE REG_NT    NT_BASE + 6*64 + 12*2
.DEFINE VID_NT    NT_BASE + 7*64 + 12*2
.DEFINE LINES_NT  NT_BASE + 8*64 + 12*2
.DEFINE FRAME_NT  NT_BASE + 10*64 + 12*2
.DEFINE PAUSE_NT  NT_BASE + 11*64 + 12*2
.DEFINE PAD_NT    NT_BASE + 12*64 + 12*2

; PAL if scanlines-per-frame >= this (NTSC = 262, PAL = 313)
.DEFINE PAL_THRESHOLD 288

; ---------------- work RAM ----------------
.ENUM $C000
frames    DW
pause_cnt DB
vlines    DW
.ENDE

.BANK 0 SLOT 0

; ---------------- entry / vectors ----------------
.ORG $0000
   di
   im 1
   ld sp, $DFF0
   jp main

.ORG $0038            ; frame interrupt (IM 1)
   push af
   push hl
   in a, (VDP_CTRL)   ; ack: read status clears the frame flag
   ld hl, (frames)
   inc hl
   ld (frames), hl
   pop hl
   pop af
   ei
   reti

.ORG $0066            ; PAUSE button -> NMI
   push af
   push hl
   ld hl, pause_cnt
   inc (hl)
   pop hl
   pop af
   retn

; ---------------- main ----------------
.ORG $0100
main:
   ; zero the counters
   ld hl, $0000
   ld (frames), hl
   ld (vlines), hl
   xor a
   ld (pause_cnt), a

   ; --- VDP registers (display off during setup) ---
   ld hl, vdp_init
   ld b, vdp_init_end - vdp_init
   ld c, VDP_CTRL
   otir

   ; --- clear all 16KB of VRAM ---
   ld hl, $4000            ; VRAM address $0000, write
   call set_vram
   ld bc, $4000
_clrv:
   xor a
   out (VDP_DATA), a
   dec bc
   ld a, b
   or c
   jr nz, _clrv

   ; --- CRAM: entry 0 black, entry 1 white, rest black ---
   ld hl, $C000            ; CRAM address 0
   call set_vram
   xor a
   out (VDP_DATA), a       ; 0: black
   ld a, $3F
   out (VDP_DATA), a       ; 1: white
   ld b, 30
   xor a
_clrc:
   out (VDP_DATA), a
   djnz _clrc

   ; --- font: expand 1bpp -> 4bpp planar (plane 0 = glyph, 1-3 = 0) ---
   ld hl, $4000            ; tile 0, write
   call set_vram
   ld hl, font
   ld bc, fontend - font
_fload:
   ld a, (hl)
   out (VDP_DATA), a
   xor a
   out (VDP_DATA), a
   out (VDP_DATA), a
   out (VDP_DATA), a
   inc hl
   dec bc
   ld a, b
   or c
   jr nz, _fload

   ; --- static text ---
   ld hl, NT_BASE + 1*64 + 2*2
   ld de, s_title
   call print_at
   ld hl, NT_BASE + 3*64 + 2*2
   ld de, s_boot
   call print_at
   ld hl, NT_BASE + 5*64 + 2*2
   ld de, s_cksum
   call print_at
   ld hl, NT_BASE + 6*64 + 2*2
   ld de, s_region
   call print_at
   ld hl, NT_BASE + 7*64 + 2*2
   ld de, s_video
   call print_at
   ld hl, NT_BASE + 8*64 + 2*2
   ld de, s_lines
   call print_at
   ld hl, NT_BASE + 10*64 + 2*2
   ld de, s_frames
   call print_at
   ld hl, NT_BASE + 11*64 + 2*2
   ld de, s_pause
   call print_at
   ld hl, NT_BASE + 12*64 + 2*2
   ld de, s_pad
   call print_at
   ld hl, NT_BASE + 15*64 + 2*2
   ld de, s_note1
   call print_at
   ld hl, NT_BASE + 16*64 + 2*2
   ld de, s_note2
   call print_at
   ld hl, NT_BASE + 18*64 + 2*2
   ld de, s_note3
   call print_at
   ld hl, NT_BASE + 21*64 + 2*2
   ld de, s_build
   call print_at

   ; --- ROM checksum self-test: sum $0000-$7FEF, compare header word ---
   ld de, $0000            ; pointer
   ld hl, $0000            ; running sum
   ld bc, $7FF0            ; byte count
_cksum:
   ld a, (de)
   push bc
   ld c, a
   ld b, 0
   add hl, bc
   pop bc
   inc de
   dec bc
   ld a, b
   or c
   jr nz, _cksum
   push hl                 ; computed sum
   ex de, hl               ; DE = computed
   ld hl, CKVAL_NT
   call set_vram_w
   ld a, d
   call print_hex8
   ld a, e
   call print_hex8
   pop hl
   ld bc, ($7FFA)          ; expected checksum from header
   or a
   sbc hl, bc
   ld de, s_ok
   jr z, _ckdone
   ld de, s_bad
_ckdone:
   ld hl, CKST_NT
   call print_at

   ; --- region detect: drive TH high then low, read back on $DD ---
   ld a, %11110101         ; TH1/TH2 outputs, level high
   out (IO_CTRL), a
   in a, (IO_DD)
   and %11000000
   cp  %11000000
   jr nz, _japan
   ld a, %01010101         ; TH level low
   out (IO_CTRL), a
   in a, (IO_DD)
   and %11000000
   jr nz, _japan
   ld de, s_export
   jr _regdone
_japan:
   ld de, s_japan
_regdone:
   ld a, $FF               ; restore port to inputs
   out (IO_CTRL), a
   ld hl, REG_NT
   call print_at

   ; --- NTSC/PAL: count scanlines (vcounter changes) in one frame ---
   ; timing-independent: any poll loop under 228 cycles samples every line
   in a, (VDP_CTRL)        ; throw away any stale frame flag first
_sync:
   in a, (VDP_CTRL)
   rlca
   jr nc, _sync            ; fresh flag = real vblank edge
   ld hl, $0000
   in a, (VCOUNT)
   ld d, a
_count:
   in a, (VCOUNT)
   cp d
   jr z, _nochg
   ld d, a
   inc hl
_nochg:
   in a, (VDP_CTRL)
   rlca
   jr nc, _count           ; next frame flag ends the window
   ld (vlines), hl
   ex de, hl
   ld hl, LINES_NT
   call set_vram_w
   ld a, d
   call print_hex8
   ld a, e
   call print_hex8
   ex de, hl
   ld bc, PAL_THRESHOLD
   or a
   sbc hl, bc
   ld de, s_ntsc
   jr c, _viddone
   ld de, s_pal
_viddone:
   ld hl, VID_NT
   call print_at

   ; --- display on, frame interrupts on ---
   ld a, $E0               ; display + frame int
   out (VDP_CTRL), a
   ld a, $81
   out (VDP_CTRL), a
   ei

; ---------------- live loop (one update per vblank) ----------------
mainloop:
   halt                    ; wake at vblank, IRQ already counted the frame

   ld hl, FRAME_NT         ; FRAMES: $XXXX
   call set_vram_w
   ld hl, (frames)
   ld a, h
   call print_hex8
   ld a, l
   call print_hex8

   ld hl, PAUSE_NT         ; PAUSE: $XX
   call set_vram_w
   ld a, (pause_cnt)
   call print_hex8

   ld hl, PAD_NT           ; PAD 1: letter = held, dot = released
   call set_vram_w
   in a, (IO_DC)
   ld c, a
   ld hl, pad_tiles
   ld b, 6
_pad:
   srl c
   ld a, (hl)
   jr nc, _padout          ; bit clear = pressed, show the letter
   ld a, 14                ; '.'
_padout:
   out (VDP_DATA), a
   xor a
   out (VDP_DATA), a
   inc hl
   djnz _pad

   jr mainloop

; ---------------- helpers ----------------

; HL = VRAM address with command bits already baked in
set_vram:
   ld a, l
   out (VDP_CTRL), a
   ld a, h
   out (VDP_CTRL), a
   ret

; HL = plain VRAM address, sets up a write
set_vram_w:
   ld a, l
   out (VDP_CTRL), a
   ld a, h
   or $40
   out (VDP_CTRL), a
   ret

; HL = name-table address, DE = zero-terminated string
print_at:
   call set_vram_w
_pr:
   ld a, (de)
   or a
   ret z
   sub 32
   out (VDP_DATA), a
   xor a
   nop
   out (VDP_DATA), a
   inc de
   jr _pr

; A = byte, VRAM write address already set; emits two hex digit tiles
print_hex8:
   push af
   rrca
   rrca
   rrca
   rrca
   call _nib
   pop af
_nib:
   and $0F
   cp 10
   jr c, _dig
   add a, 7                ; A-F: tile 33 + n-10 = n + 23
_dig:
   add a, 16               ; 0-9: tile 16 + n
   out (VDP_DATA), a
   xor a
   nop
   out (VDP_DATA), a
   ret

; ---------------- data ----------------

; reg 0: mode 4 + M2; reg 1: display off (bit7 set per docs);
; regs 2-6: table bases; reg 7: border; 8/9: scroll; 10: line int off
vdp_init:
   .DB $06, $80
   .DB $A0, $81
   .DB $FF, $82            ; name table $3800
   .DB $FF, $83
   .DB $FF, $84
   .DB $FF, $85            ; sprite attributes $3F00 (unused)
   .DB $FF, $86
   .DB $00, $87            ; border = sprite palette 0 (black)
   .DB $00, $88
   .DB $00, $89
   .DB $FF, $8A
vdp_init_end:

s_title:  .DB "SMS DIAGNOSTIC - HELLO WORLD", 0
s_boot:   .DB "BOOT: OK (Z80+VDP+VRAM+RAM)", 0
s_cksum:  .DB "CKSUM:    $", 0
s_region: .DB "REGION:", 0
s_video:  .DB "VIDEO:", 0
s_lines:  .DB "LINES:    $", 0
s_frames: .DB "FRAMES:   $", 0
s_pause:  .DB "PAUSE:    $", 0
s_pad:    .DB "PAD 1:", 0
s_ok:     .DB "OK", 0
s_bad:    .DB "BAD", 0
s_export: .DB "EXPORT", 0
s_japan:  .DB "JAPANESE", 0
s_ntsc:   .DB "NTSC 60HZ", 0
s_pal:    .DB "PAL 50HZ", 0
s_note1:  .DB "FRAMES = VDP IRQ, PAUSE = NMI,", 0
s_note2:  .DB "PAD BITS LIVE FROM PORT $DC.", 0
s_note3:  .DB "IF YOU CAN READ THIS, IT WORKS", 0
s_build:  .DB "32X-BUILDER SMS 2026-08-09", 0

pad_tiles:                 ; tiles for U D L R 1 2 (ASCII - 32)
   .DB 'U'-32, 'D'-32, 'L'-32, 'R'-32, '1'-32, '2'-32

font:
.INCBIN "font_1bpp.bin"
fontend:
