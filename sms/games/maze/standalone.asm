; =====================================================================
; STANDALONE MAZE — the mini-game as a real .sms cartridge image.
;
; The fast iteration loop (Mike, 2026-08-14): open games/maze/maze.sms
; in Ares' MASTER SYSTEM core (which has real mode 4 — it is the MD
; core's mode-4 stub that is broken) instead of booting the whole 32X
; build and walking to the console. Same game.asm byte-for-byte logic;
; this file is a thin shim playing the roles the 32X splits between the
; 68K and the SH-2:
;   - mode-4 VDP init, palette + planar tiles into VRAM (tiles_sms.inc,
;     generated from sms/tileset.json alongside every other consumer)
;   - a demo level pack copied to RAM where the 68K would patch it
;   - per-loop sa_frame: vblank poll -> pad byte, FRAME_MBX tick, and a
;     TILEBUF -> name-table blit when DIRTY (the 68K's bridge job)
;   - psg_write goes OUT ($7F) instead of the Genesis $7F11 mapping
;
; The game's RAM contract shifts wholesale to $C000 (RB in game.asm);
; the layout offsets are identical. Pad: UDLR map 1:1, button 1 = A
; (begin exercise), button 2 = START (debrief exit / terminal return).
;
; Build: make maze-ares (sms/Makefile). NOT sim-covered (the subset
; interpreter has no ports); the harness build stays the verified one.
; The blit runs unsynchronized VRAM writes past vblank's end — fine in
; an emulator, would need budgeting on real hardware.
; =====================================================================

.DEFINE STANDALONE

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

.SMSHEADER
   PRODUCTCODE 26, 70, 2
   VERSION 0
   REGIONCODE 4        ; SMS export
   RESERVEDSPACE $FF, $FF
   ROMSIZE $C          ; 32KB
.ENDSMS

.DEFINE VDP_DATA  $BE
.DEFINE VDP_CTRL  $BF
.DEFINE IO_DC     $DC   ; pad 1: bits 0-5 = U D L R 1 2, active low
.DEFINE WALK_DIV  1     ; frames between steps (see sa_step_axis)
.DEFINE WALK_PX   2     ; pixels per step. Travel and the LEG CADENCE are
                        ; separate dials: the cadence Mike signed off on is
                        ; one walk frame per 8 real frames, and sa_sp_anim
                        ; holds it there by advancing per WALK_PX*8 pixels.
                        ; Raising WALK_PX therefore carries him farther per
                        ; stride WITHOUT speeding his legs up. A 32px cell
                        ; takes 32/WALK_PX frames; MOVE_COOL is that - 1.
.DEFINE NT_BASE   $3800

; ---- scroll-engine state (RAM below the game's RB+$1900 contract) ----
; The maze runs in SCROLL MODE: a pixel camera over the 1024x1024-px
; map (128x128 tiles), VDP regs 8/9 pan, one seam column/row streamed
; per 8px tile crossing (the Snail Maze pattern, srcref/sms/bios13.asm
; ~line 2069), and the player as 16 hardware sprites at world - camera.
; Font screens (terminal/diag/card/escape) stay on the DIRTY-blit path.
.DEFINE SA_MODE   $C100 ; 0 = blit (font screens), 1 = scroll (maze)
.DEFINE SA_PXW    $C101 ; player world x, 16-bit (cell*32, eased)
.DEFINE SA_PYW    $C103 ; player world y, 16-bit
.DEFINE SA_TXW    $C105 ; slide target x (VAR_PX*32)
.DEFINE SA_TYW    $C107 ; slide target y
.DEFINE SA_CAMX   $C109 ; camera x (0..768)
.DEFINE SA_CAMY   $C10B ; camera y (0..832)
.DEFINE SA_PCX    $C10D ; last player cell seen (move detection)
.DEFINE SA_PCY    $C10E
.DEFINE SA_LCOL   $C10F ; camx>>3 last frame (column-seam trigger)
.DEFINE SA_LROW   $C110 ; camy>>3 last frame (row-seam trigger)
.DEFINE SA_TMP    $C111 ; stream scratch (row mod 28)
.DEFINE SA_CKX    $C112 ; 1-cell classifier cache: cell x
.DEFINE SA_CKY    $C113 ;   cell y
.DEFINE SA_CKK    $C114 ;   kind
.DEFINE SA_CKV    $C115 ;   1 = cache valid
.DEFINE SA_FRAME  $C116 ; walk-cycle frame offset (frame*16 tile ids)
.DEFINE SA_SUB    $C119 ; walk pace divider: frames until the next 1px
                        ; step, counted down by sa_slide_step. See
                        ; WALK_DIV for the pace itself.
.DEFINE SA_WGR    $C11A ; walk grace: frames the walk pose survives after
                        ; a glide lands. The shim sets the NEXT cell's
                        ; target one frame after arrival, and that gap read
                        ; as "at target" — the legs snapped to the idle
                        ; pose for a single frame at every cell boundary,
                        ; a hitch once per cell in a continuous walk.
.DEFINE SA_NTP    $C117 ; column streamer: running name-table pointer.
                        ; Recomputing $7800 + nr*64 + col*2 per tile cost
                        ; six shifts x 28 rows and pushed the seam frame
                        ; over the Z80's ~5,400-instruction frame budget;
                        ; walking it forward by 64 (wrapping at row 28)
                        ; keeps the paint inside one frame.

.BANK 0 SLOT 0

; ---------------- entry / vectors ----------------
.ORG $0000
   di
   im 1
   jp sa_boot

.ORG $0038            ; frame interrupt: never enabled; stray ack + return
   push af
   in a, (VDP_CTRL)
   pop af
   ei
   reti

.ORG $0066            ; PAUSE -> NMI: ignore
   retn

; ---------------- boot shim ----------------
.ORG $0100
sa_boot:
   ld sp, $DFF0        ; shim stack; the game re-points it to RB+$1F80

   ld a, $FF           ; I/O control: TR/TH all inputs (hello.asm's
   out ($3F), a        ; hardware-validated init; emulators don't care,
                       ; real pads do)

   ; VDP registers, display off
   ld hl, sa_vdp_init
   ld b, sa_vdp_init_end - sa_vdp_init
   ld c, VDP_CTRL
   otir

   ; clear VRAM
   ld hl, $4000
   call sa_set_vram
   ld bc, $4000
sa_clrv:
   xor a
   out (VDP_DATA), a
   dec bc
   ld a, b
   or c
   jr nz, sa_clrv

   ; CRAM: BG palette from the tileset, MIRRORED into the sprite half —
   ; the player sprites use the same 15 colors (entry 0 stays the
   ; transparent/border black either way)
   ld hl, $C000
   call sa_set_vram
   ld c, 2
sa_pal2:
   ld hl, sms_bg_palette
   ld b, 16
sa_pal:
   ld a, (hl)
   out (VDP_DATA), a
   inc hl
   djnz sa_pal
   dec c
   jr nz, sa_pal2

   ; boot font -> VRAM tiles 0.. (index IS the TILEBUF id)
   ld hl, $4000
   call sa_set_vram
   ld hl, sms_font_planar
   ld bc, SMS_FONT_COUNT * 32
   call sa_vram_copy
   ; art tiles -> VRAM tile 128 (art id = 128 + index, same as TILEBUF)
   ld hl, $4000 | (128 * 32)
   call sa_set_vram
   ld hl, sms_art_planar
   ld bc, SMS_ART_COUNT * 32
   call sa_vram_copy
   ; player sprite tiles: the 4-frame walk cycle (64 tiles, 192..255)
   ld hl, $4000 | (SMS_SPRITE_TILE_BASE * 32)
   call sa_set_vram
   ld hl, sms_player_sprite_planar
   ld bc, 64 * 32
   call sa_vram_copy

   ; demo level pack -> the three RAM ranges the 68K would patch
   ld hl, sms_demo_pack
   ld de, MAP_BITS
   ld bc, 148
   ldir
   ld de, PEDGE_N
   ld bc, 132
   ldir
   ld de, PEDGE_W
   ld bc, 160
   ldir

   ; mailboxes + scroll-engine state start clean (RAM powers up as noise)
   xor a
   ld (PAD_MBX), a
   ld (FRAME_MBX), a
   ld (DIRTY_MBX), a
   ld (STATE_MBX), a
   ld (SA_MODE), a
   ld (SA_CKV), a
   ld (SA_WGR), a
   ld (SA_SUB), a      ; dirty RAM here stalls the first step for as many
                       ; frames as the garbage byte says (it read $55 once
                       ; — 85 frames of the player standing still)

   ; display on (no frame int — sa_frame polls the status flag)
   ld a, $C0
   out (VDP_CTRL), a
   ld a, $81
   out (VDP_CTRL), a
   in a, (VDP_CTRL)    ; throwaway: the flag accumulates while unread
   jp entry            ; the game boots exactly as it does on the Z80

; ---------------- the 68K's per-frame bridge job ----------------
; Called from the game's main loop each iteration. Most calls see no
; vblank and return. On the flag, dispatch by state: the maze (playing)
; runs the scroll engine; every text screen runs the pad+tick+blit
; bridge exactly as before.
sa_frame:
   in a, (VDP_CTRL)
   and $80
   ret z
   ld a, (VAR_GSTATE)
   cp 1
   jr nz, sa_font_frame
   ld a, (STATE_MBX)   ; maze state but escaped -> debrief text screen
   or a
   jp z, sa_maze
sa_font_frame:
   ld a, (SA_MODE)
   or a
   call nz, sa_scroll_exit
   call sa_read_pad
   ld a, (FRAME_MBX)
   inc a
   ld (FRAME_MBX), a
   ld a, (DIRTY_MBX)
   or a
   ret z
   xor a
   ld (DIRTY_MBX), a
   ld hl, $4000 | NT_BASE
   call sa_set_vram
   ld hl, TILEBUF
   ld bc, 768
sa_blit:
   ld a, (hl)
   out (VDP_DATA), a
   xor a
   out (VDP_DATA), a   ; name-table high byte: BG palette, no flips
   inc hl
   dec bc
   ld a, b
   or c
   jr nz, sa_blit
   ret

; pad: UDLR are the same bits in both worlds; 1 -> A, 2 -> START
sa_read_pad:
   in a, (IO_DC)
   cpl
   ld b, a
   and $0F
   ld c, a
   ld a, b
   and $10             ; button 1 -> A ($40)
   add a, a
   add a, a
   or c
   ld c, a
   ld a, b
   and $20             ; button 2 -> START ($80)
   add a, a
   add a, a
   or c
   ld (PAD_MBX), a
   ret

; ================= SCROLL MODE (the maze) ============================
sa_maze:
   ld a, (SA_MODE)
   or a
   call z, sa_scroll_enter
   ld a, (FRAME_MBX)   ; the game still paces (and plays music) on this
   inc a
   ld (FRAME_MBX), a
   xor a               ; TILEBUF is not the display here — drop DIRTY
   ld (DIRTY_MBX), a
   ; ---- sliding toward the target? ----
   ld hl, (SA_PXW)
   ld de, (SA_TXW)
   or a
   sbc hl, de
   ld a, h
   or l
   jr nz, sa_sliding
   ld hl, (SA_PYW)
   ld de, (SA_TYW)
   or a
   sbc hl, de
   ld a, h
   or l
   jr nz, sa_sliding
   ; ---- idle: pick up a fresh game move, else pass the real pad ----
   ld a, (VAR_PX)
   ld hl, SA_PCX
   cp (hl)
   jr nz, sa_new_target
   ld a, (VAR_PY)
   ld hl, SA_PCY
   cp (hl)
   jr nz, sa_new_target
   call sa_read_pad
   jr sa_render
sa_new_target:
   ld a, (VAR_PX)
   ld (SA_PCX), a
   call sa_cell_to_px
   ld (SA_TXW), hl
   ld a, (VAR_PY)
   ld (SA_PCY), a
   call sa_cell_to_px
   ld (SA_TYW), hl
sa_sliding:
   call sa_slide_step
   ; Input is gated WHILE gliding, but reopens on the frame the glide
   ; LANDS. The shim runs before the game logic, so its view of VAR_PX
   ; is one frame old: gating the landing frame too cost a dead frame
   ; per cell (pan cadence 4,4,4,4,0 — a 12 Hz hitch). Handing the real
   ; pad over on arrival lets the game commit the next cell in the same
   ; tick, so the next glide starts immediately: 4,4,4,4,4,...
   ld hl, (SA_PXW)
   ld de, (SA_TXW)
   or a
   sbc hl, de
   ld a, h
   or l
   jr nz, sa_gate
   ld hl, (SA_PYW)
   ld de, (SA_TYW)
   or a
   sbc hl, de
   ld a, h
   or l
   jr nz, sa_gate
   call sa_read_pad    ; landed: the game may step this very frame
   jr sa_render
sa_gate:
   xor a               ; mid-glide: held pads chain by the move cooldown
   ld (PAD_MBX), a
sa_render:
   call sa_update_cam
   call sa_seams
   call sa_set_scroll
   call sa_sprites
   ret

sa_cell_to_px:         ; a = cell (0..31) -> hl = a*32 (32px cells, v4)
   ld l, a
   ld h, 0
   add hl, hl
   add hl, hl
   add hl, hl
   add hl, hl
   add hl, hl
   ret

; Step each world axis ONE pixel toward its target, and only every
; WALK_DIV frames — the finest motion the hardware can express, paced to
; a human walk. A 32px cell therefore takes 32*WALK_DIV frames. Only one
; axis ever differs, and targets are whole cells, so there is no
; overshoot. This also spreads the seam load: a tile boundary now falls
; every 8*WALK_DIV frames instead of every other one.
sa_slide_step:
   ld a, (SA_SUB)      ; pace divider: move 1px every WALK_DIV frames
   or a
   jr z, sa_ss_move
   dec a
   ld (SA_SUB), a
   ret
sa_ss_move:
   ld a, WALK_DIV - 1
   ld (SA_SUB), a
   ld hl, (SA_PXW)
   ld de, (SA_TXW)
   call sa_step_axis
   ld (SA_PXW), hl
   ld hl, (SA_PYW)
   ld de, (SA_TYW)
   call sa_step_axis
   ld (SA_PYW), hl
   ret
sa_step_axis:          ; hl = cur, de = target -> hl stepped 1 toward de
   push hl
   or a
   sbc hl, de
   pop hl
   ret z
   jr c, sa_step_up
   ld de, -WALK_PX
   add hl, de
   ret
sa_step_up:
   ld de, WALK_PX
   add hl, de
   ret

; camera = clamp(player - screen-center offset, 0, max)
sa_update_cam:
   ld hl, (SA_PXW)
   ld de, 112          ; 128 - half a 32px cell
   ld bc, 768          ; 1024 - 256
   call sa_clamp_axis
   ld (SA_CAMX), hl
   ld hl, (SA_PYW)
   ld de, 80           ; 96 - 16
   ld bc, 832          ; 1024 - 192
   call sa_clamp_axis
   ld (SA_CAMY), hl
   ret
sa_clamp_axis:         ; hl = pos, de = offset, bc = max -> hl = camera
   or a
   sbc hl, de
   jr nc, sa_cl_lo
   ld hl, 0
   ret
sa_cl_lo:
   push hl
   or a
   sbc hl, bc
   pop hl
   ret c
   ld h, b
   ld l, c
   ret

; write regs 8/9 from the camera. Value byte first, then $80|reg (the
; SMSlib/BIOS order). Reg 8 is the NEGATED camera x (the register
; shifts screen content right); reg 9 is camera y wrapped at 224.
sa_set_scroll:
   ld a, (SA_CAMX)     ; low byte is all that matters mod 256
   neg
   di
   out (VDP_CTRL), a
   ld a, $88
   out (VDP_CTRL), a
   ei
   ld hl, (SA_CAMY)    ; 0..832: subtract 224 until < 224
   ld de, -224
sa_ss_wrap:
   ld a, h
   or a
   jr nz, sa_ss_sub
   ld a, l
   cp 224
   jr c, sa_ss_go
sa_ss_sub:
   add hl, de
   jr sa_ss_wrap
sa_ss_go:
   ld a, l
   di
   out (VDP_CTRL), a
   ld a, $89
   out (VDP_CTRL), a
   ei
   ret

; ---- seam streaming: fired when the camera crosses a tile boundary --
sa_seams:
   ld a, (SA_CAMX)     ; colbase = camx>>3 (camx <= 768, so 16-bit shift)
   ld l, a
   ld a, (SA_CAMX+1)
   ld h, a
   srl h
   rr l
   srl h
   rr l
   srl h
   rr l
   ld a, l             ; new colbase (0..96)
   ld hl, SA_LCOL
   cp (hl)
   jr z, sa_seam_v
   ld b, a             ; moving right: incoming col = old + 32 = new + 31
   jr c, sa_seam_left  ; new < old: moving left, incoming col = new
   ld (hl), a
   add a, 31
   ld b, a
   call sa_stream_col
   jr sa_seam_v
sa_seam_left:
   ld (hl), a
   call sa_stream_col  ; b = new colbase
sa_seam_v:
   ld a, (SA_CAMY)
   ld l, a
   ld a, (SA_CAMY+1)
   ld h, a
   srl h
   rr l
   srl h
   rr l
   srl h
   rr l
   ld a, l             ; new rowbase (0..104)
   ld hl, SA_LROW
   cp (hl)
   ret z
   ld c, a
   jr c, sa_seam_up    ; new < old: moving up, incoming row = new
   ld (hl), a
   ld a, c
   add a, 27           ; moving down: incoming row = new + 27
   ld c, a
   jp sa_stream_row
sa_seam_up:
   ld (hl), a
   jp sa_stream_row    ; c = new rowbase

; stream one name-table COLUMN: b = map tile col (0..127+). 28 entries
; at name col b&31; name row nr holds map row base+k where
; nr = (base%28 + k) % 28 — the invariant the row seam maintains too.
sa_stream_col:
   ld a, (SA_CAMY)
   ld l, a
   ld a, (SA_CAMY+1)
   ld h, a
   srl h
   rr l
   srl h
   rr l
   srl h
   rr l
   ld c, l             ; c = base
   ld a, c
sa_sc_mod:
   sub 28
   jr nc, sa_sc_mod
   add a, 28
   ld (SA_TMP), a      ; nr0 = base mod 28
   ld l, a             ; SA_NTP = $7800 + nr0*64 + (b&31)*2, ONCE — the
   ld h, 0             ; loop then walks it a row at a time
   add hl, hl
   add hl, hl
   add hl, hl
   add hl, hl
   add hl, hl
   add hl, hl
   ld a, b
   and 31
   add a, a
   ld e, a
   ld d, 0
   add hl, de
   ld de, $7800
   add hl, de
   ld (SA_NTP), hl
   ld a, (SA_TMP)
   ld d, a             ; d = nr, incremented and wrapped in the loop
   ld e, 0             ; e = k
sa_sc_loop:
   ld hl, (SA_NTP)
   di
   ld a, l
   out (VDP_CTRL), a
   ld a, h
   out (VDP_CTRL), a
   ei
   ld a, c             ; tile row = base + k
   add a, e
   call sa_cell_tile   ; preserves bc, de -> a = tile id
   out (VDP_DATA), a
   xor a
   inc hl              ; VRAM pacing (the crt0_sms 27-cycle idiom)
   dec hl
   out (VDP_DATA), a
   ld a, d             ; next name-table row: +64, wrapping the 28-row
   inc a               ; window back to its top
   cp 28
   jr c, sa_sc_adv
   xor a
   ld d, a
   ld a, b
   and 31
   add a, a
   ld l, a
   ld h, $78
   ld (SA_NTP), hl
   jr sa_sc_next
sa_sc_adv:
   ld d, a
   ld hl, (SA_NTP)     ; reload: sa_cell_tile clobbers hl (cell_kind_bg
   ld a, l             ; does), so the pointer cannot live in it across
   add a, 64           ; the call — only the VRAM pacing inc/dec may
   ld l, a
   jr nc, sa_sc_store
   inc h
sa_sc_store:
   ld (SA_NTP), hl
sa_sc_next:
   inc e
   ld a, e
   cp 28
   jr c, sa_sc_loop
   ret

; stream one name-table ROW: c = map tile row (0..131, clamped in the
; classifier). 32 entries at name row c%28; name col nc holds map col
; colbase + k where nc = (colbase + k) & 31.
sa_stream_row:
   ld a, c
sa_sr_mod:
   sub 28
   jr nc, sa_sr_mod
   add a, 28
   ld (SA_TMP), a      ; row mod 28
   ld a, (SA_CAMX)
   ld l, a
   ld a, (SA_CAMX+1)
   ld h, a
   srl h
   rr l
   srl h
   rr l
   srl h
   rr l
   ld b, l             ; b = colbase
   ld e, 0             ; e = k
sa_sr_loop:
   push de
   ld a, b
   add a, e
   and 31              ; name col
   add a, a
   ld e, a
   ld a, (SA_TMP)      ; hl = $7800 + (row%28)*64 + nc*2
   ld l, a
   ld h, 0
   add hl, hl
   add hl, hl
   add hl, hl
   add hl, hl
   add hl, hl
   add hl, hl
   ld d, 0
   add hl, de
   ld de, $7800
   add hl, de
   di
   ld a, l
   out (VDP_CTRL), a
   ld a, h
   out (VDP_CTRL), a
   ei
   pop de
   push bc
   ld a, b             ; map col = colbase + k
   add a, e
   ld b, a
   ld a, c             ; map row
   call sa_cell_tile
   pop bc
   out (VDP_DATA), a
   xor a
   inc hl
   dec hl
   out (VDP_DATA), a
   inc e
   ld a, e
   cp 32
   jr c, sa_sr_loop
   ret

; map tile (b = col, a = row) -> a = tile id, via the game's shared
; background classifier + the metatile table. Rows past the map bottom
; return tile 0 (they are never visible — the camera clamp keeps them
; off-screen; this is belt-and-braces). One-cell cache: streams walk
; runs of 4 tiles per cell, so 3 of 4 lookups hit.
sa_cell_tile:
   push bc
   push de
   cp 128              ; 32 cells x 4 tile rows (v4: 32px cells)
   jr c, sa_ct_in
   pop de
   pop bc
   xor a
   ret
sa_ct_in:
   ld c, a             ; c = tile row
   push bc             ; save col/row
   ld e, b
   srl e
   srl e               ; e = cell x (tile col / 4)
   srl c
   srl c               ; c = cell y
   ld a, (SA_CKV)      ; cache probe
   or a
   jr z, sa_ct_miss
   ld a, (SA_CKX)
   cp e
   jr nz, sa_ct_miss
   ld a, (SA_CKY)
   cp c
   jr nz, sa_ct_miss
   ld a, (SA_CKK)
   jr sa_ct_kind
sa_ct_miss:
   call cell_kind_bg   ; preserves e, c; clobbers d, hl
   push af
   ld a, e
   ld (SA_CKX), a
   ld a, c
   ld (SA_CKY), a
   pop af
   ld (SA_CKK), a
   push af
   ld a, 1
   ld (SA_CKV), a
   pop af
sa_ct_kind:
   ld l, a             ; hl = METATILES + kind*16 (19 x 16 = 304B, so
   ld h, 0             ; this no longer fits an 8-bit offset)
   add hl, hl
   add hl, hl
   add hl, hl
   add hl, hl
   ld de, METATILES
   add hl, de
   pop bc              ; b = tile col, c = tile row
   ld a, c
   and 3
   add a, a
   add a, a            ; (row & 3) * 4
   ld e, a
   ld a, b
   and 3
   add a, e
   ld e, a
   ld d, 0
   add hl, de
   ld a, (hl)
   pop de
   pop bc
   ret

; ---- the player: 16 8x8 sprites in a 4x4 block at world - camera ----
; Walk cycle: gliding picks frame ((PXW+PYW)>>3)&3 — a new frame every
; 8px of travel, the classic cadence; idle rests on frame 0.
; The 32x32 hero STRADDLES 16px cells (Predator 2 geometry): x centered
; over the cell (-8), feet on the cell's bottom edge (-16).
sa_sprites:
   ld hl, (SA_PXW)
   ld de, (SA_TXW)
   or a
   sbc hl, de
   ld a, h
   or l
   jr nz, sa_sp_anim
   ld hl, (SA_PYW)
   ld de, (SA_TYW)
   or a
   sbc hl, de
   ld a, h
   or l
   jr nz, sa_sp_anim
   ld a, (SA_WGR)      ; at target: hold the walk pose briefly, so
   or a                ; chaining cells does not flash the idle frame
   jr z, sa_sp_idle
   dec a
   ld (SA_WGR), a
   jr sa_sp_walk
sa_sp_idle:
   xor a
   jr sa_sp_setf
sa_sp_anim:
   ld a, 3
   ld (SA_WGR), a
sa_sp_walk:
   ld hl, (SA_PXW)
   ld de, (SA_PYW)
   add hl, de
   ld a, l             ; one walk frame per 16px of travel — at WALK_PX=2
   rrca                ; that is a frame every 8 real frames, the cadence
   rrca                ; the 1px/8px version had. Legs unchanged, ground
   rrca                ; covered doubled.
   rrca
   and 3
   add a, a
   add a, a
   add a, a
   add a, a                ; frame * 16 tile ids
sa_sp_setf:
   ld (SA_FRAME), a
   ld hl, (SA_PXW)
   ld de, (SA_CAMX)
   or a
   sbc hl, de
   ld a, l             ; a 32px sprite on a 32px cell: no straddle, the
   ld b, a             ; figure IS the cell now (b = screen x)
   ld hl, (SA_PYW)
   ld de, (SA_CAMY)
   or a
   sbc hl, de
   ld a, l             ; feet on the cell's bottom edge — with a 32px
   ld c, a             ; cell that is the cell's own top (c = screen y)
   ld hl, $7F00        ; SAT Y block
   call sa_set_vram
   ld d, 0
sa_sp_y:
   ld a, d
   and $0C             ; (i>>2)*4
   add a, a            ; *8 = sprite row offset
   add a, c
   dec a               ; the VDP draws at Y+1
   out (VDP_DATA), a
   inc d
   ld a, d
   cp 16
   jr c, sa_sp_y
   ld a, $D0           ; sprite-list terminator
   out (VDP_DATA), a
   ld hl, $7F80        ; SAT X/N pairs
   call sa_set_vram
   ld hl, SA_FRAME
   ld d, 0
sa_sp_xn:
   ld a, d
   and $03
   add a, a
   add a, a
   add a, a            ; (i&3)*8
   add a, b
   out (VDP_DATA), a
   ld a, d
   add a, SMS_SPRITE_TILE_BASE
   add a, (hl)         ; + the walk frame's 16-tile block
   push af             ; VRAM pacing between the pair
   pop af
   out (VDP_DATA), a
   inc d
   ld a, d
   cp 16
   jr c, sa_sp_xn
   ret

; ---- mode transitions ----
; Enter: seed the world/camera state from the game's cells, paint the
; whole 32x28 name table display-off (2-3 dark frames — the classifier
; cache keeps it that cheap), arm the column-0 mask, sprites on.
sa_scroll_enter:
   ld a, 1
   ld (SA_MODE), a
   xor a
   ld (SA_CKV), a
   ld a, (VAR_PX)
   ld (SA_PCX), a
   call sa_cell_to_px
   ld (SA_PXW), hl
   ld (SA_TXW), hl
   ld a, (VAR_PY)
   ld (SA_PCY), a
   call sa_cell_to_px
   ld (SA_PYW), hl
   ld (SA_TYW), hl
   call sa_update_cam
   ; seam-trigger latches match the fresh camera
   ld a, (SA_CAMX)
   ld l, a
   ld a, (SA_CAMX+1)
   ld h, a
   srl h
   rr l
   srl h
   rr l
   srl h
   rr l
   ld a, l
   ld (SA_LCOL), a
   ld a, (SA_CAMY)
   ld l, a
   ld a, (SA_CAMY+1)
   ld h, a
   srl h
   rr l
   srl h
   rr l
   srl h
   rr l
   ld a, l
   ld (SA_LROW), a
   ld a, $A0           ; display off for the full paint
   out (VDP_CTRL), a
   ld a, $81
   out (VDP_CTRL), a
   ; paint all 28 rows: row seam invariant, map row = rowbase + k
   ld a, (SA_LROW)
   ld b, a
   ld e, 0
sa_se_paint:
   push de
   push bc
   ld a, b
   add a, e
   ld c, a
   call sa_stream_row
   pop bc
   pop de
   inc e
   ld a, e
   cp 28
   jr c, sa_se_paint
   call sa_set_scroll
   call sa_sprites
   ld a, $26           ; mode 4 + mask column 0 (hides the seam pop)
   out (VDP_CTRL), a
   ld a, $80
   out (VDP_CTRL), a
   ld a, $C0           ; display back on
   out (VDP_CTRL), a
   ld a, $81
   out (VDP_CTRL), a
   ret

; Exit: scroll home, sprites off, plain reg 0 — the DIRTY blit that
; follows (escape/terminal text) owns the screen again.
sa_scroll_exit:
   xor a
   ld (SA_MODE), a
   di
   out (VDP_CTRL), a   ; reg 8 = 0
   ld a, $88
   out (VDP_CTRL), a
   xor a
   out (VDP_CTRL), a   ; reg 9 = 0
   ld a, $89
   out (VDP_CTRL), a
   ei
   ld a, $06           ; column 0 visible again
   out (VDP_CTRL), a
   ld a, $80
   out (VDP_CTRL), a
   ld hl, $7F00
   call sa_set_vram
   ld a, $D0           ; empty sprite list
   out (VDP_DATA), a
   ret

sa_set_vram:
   ld a, l
   out (VDP_CTRL), a
   ld a, h
   out (VDP_CTRL), a
   ret

sa_vram_copy:          ; hl = src, bc = len -> VDP data port
   ld a, (hl)
   out (VDP_DATA), a
   inc hl
   dec bc
   ld a, b
   or c
   jr nz, sa_vram_copy
   ret

; reg 0: mode 4; reg 1: display off during setup; name table $3800;
; sprites unused; border black (mirrors hello.asm's proven table)
sa_vdp_init:
   .DB $06, $80
   .DB $A0, $81
   .DB $FF, $82
   .DB $FF, $83
   .DB $FF, $84
   .DB $FF, $85
   .DB $FB, $86            ; sprite patterns in the FIRST 8KB: the player
                           ; sprite tiles live at SMS_SPRITE_TILE_BASE
   .DB $00, $87
   .DB $00, $88
   .DB $00, $89
   .DB $FF, $8A
sa_vdp_init_end:

; ---------------- the game itself ----------------
; STANDALONE is defined, so game.asm skips its memory map and $0000 org,
; RB lifts every RAM address to $C000, and psg_write goes OUT ($7F).
; Its tiles.inc lands the metatile table at ROM $1700, past this code.
.INCLUDE "game.asm"

; tiles.inc left the write head at ~$1830; generated data goes after it
.ORG $1840
.INCLUDE "tiles_sms.inc"
