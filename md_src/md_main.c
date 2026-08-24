#include "common.h"
#include "z80_sms_hello.h"
#include "z80_sms_game.h"

// 32X COMM
static volatile uint16_t* const mars_comm0  = (uint16_t*) MARS_COMM0;
static volatile uint16_t* const mars_comm2  = (uint16_t*) MARS_COMM2;
static volatile uint16_t* const mars_comm6  = (uint16_t*) MARS_COMM6;
static volatile uint16_t* const mars_comm8  = (uint16_t*) MARS_COMM8;
static volatile uint16_t* const mars_comm10 = (uint16_t*) MARS_COMM10;
// 16-bit ON PURPOSE: a 32-bit tick write at $A1512C spans COMM14 too,
// and clobbered the SMS maze status published there every vblank (the
// smooth renderer flickered against the TILEBUF path — Mike's captures
// 40-46). Every SH-2 consumer of the tick only tests "changed since
// last read", so a 16-bit counter (wraps ~18 min) is equivalent there.
static volatile uint16_t* const mars_comm12 = (uint16_t*) MARS_COMM12;

// VDP
static volatile uint16_t* const vdp_data_port = (uint16_t*) VDP_DATA_PORT;
static volatile uint16_t* const vdp_ctrl_port = (uint16_t*) VDP_CTRL_PORT;
static volatile uint32_t* const vdp_ctrl_wide = (uint32_t*) VDP_CTRL_PORT;

// External functions
extern uint16_t read_joypad(uint8_t player);

uint16_t timer = 0;   // 16-bit with the tick write — see mars_comm12
uint16_t vramOffset = 0;
// SMS mode active: the VDP is in mode 4, where the mode-5 vblank status
// bit the main loop paces on NEVER sets — polling it livelocks the loop
// BEFORE the pad-publish lines and the player can't exit (COMM8 freezes).
// While set, the loop paces on a crude delay instead and keeps serving.
static uint8_t sms_active = 0;

// SMS mini-game bridge state. The staged map is the "ROM patch": the SH-2
// streams the live level (132 bytes: 1bpp world_map + spawn + exit) into
// sms_game_map via command 0x0B words, and the game-boot command writes it
// into the Z80 blob's MAP region after the code copy — the Master System
// program wakes up already holding whatever map the player was standing in.
static uint8_t sms_game_on = 0;
static uint8_t sms_debrief = 0;    // Z80 STATE_MBX==1: the Z80 owns START
                                   // (debrief OR diagnostics page; COMM8 b14)
static uint8_t sms_game_map[Z80_GAME_MAP_LEN];
static uint8_t sms_game_tiles[Z80_GAME_TILEBUF_LEN];
// GAME-ON-GLASS: the Z80 runs the mini-game HEADLESS (pad held at 0, no
// name-table blit — the MD text layer belongs to the 3D HUD) and this
// side free-runs a broadcast of TILEBUF over spare COMM registers:
// COMM6 = tile pair, COMM10 = pair index (pad-2 publishing pauses; no
// gameplay reads it). One pair per do_commands call — thousands per
// frame — and the SH-2 samples a few dozen, double-reading the index
// for coherence. No handshake anywhere: the joypad-bridge starvation
// history says COMM request/response under render load loses.
// Broadcast payload is an OCCUPANCY BITMAP, not the tiles: the glass render
// only asks "is this cell lit", so one bit per cell turns 384 index+word
// slots into 48 — the whole picture crosses in a couple of frames instead
// of tens, on a channel where every sample is a race we might lose.
#define SMS_GLASS_WORDS (Z80_GAME_TILEBUF_LEN / 16)   /* 768 cells -> 48 words */
// FULLSCREEN-ON-32X (command 21). The modal mini-game's picture has always
// reached the screen as MD plane-B tiles composited over a black 32X frame.
// This mode sends the RAW TILE IDS to the SH-2 instead, which renders them with
// its own 8x8 font -- the same font those ids index, per sms/DESIGN.md -- so the
// picture becomes 32X framebuffer pixels. That matters beyond fullscreen: the
// zoom-into-the-glass transition needs its source and its destination to be the
// SAME renderer, or the arrival is a cut.
//
// Payload is 2 ids per word (768 cells -> 384 slots), not the glass path's
// 1-bit occupancy: at 8x8 per cell there is room for a glyph, and at the tube's
// measured 1.06 texels per cell there is not. Same free-running index+word
// rotation, same no-handshake rule.
#define SMS_TILE_WORDS (Z80_GAME_TILEBUF_LEN / 2)   /* 768 ids -> 384 words */
/* 1: every do_commands call sends a slot. History: /8 starved the reader
 * (partial pictures, title bleeding through) and didn't fix the PSG clipping
 * it was aimed at; /2 was the compromise while partial paints were still
 * possible. The reader now promotes whole pictures only, so the divider buys
 * no correctness -- it only stretches the rotation, and the rotation period
 * IS the input-to-photon delay the fullscreen game plays under (Mike felt
 * it). Full rate halves that. If the PSG loses note-offs again, THIS is the
 * knob that moved (sysreg pressure on the vblank poll). */
#define SMS_TILE_DIV   1      /* broadcast one slot per N do_commands (pow2) */
static uint16_t sms_tile_div = 0;
static uint8_t  sms_tile_bcast = 0;
// DIRTY-EPOCH (cmd 21 bit 1; TESTING>EPOCH, default OFF). Instead of
// rotating all 384 slots forever, broadcast only the slots the last dirty
// frame CHANGED, each tagged with a 6-bit epoch, and a MARKER word after
// every pass of the set carrying the set's size — the reader presents an
// epoch atomically once it holds all of it. Payloads stay ABSOLUTE content
// (index + data), never diffs, so a lost or skipped epoch merely leaves
// those cells stale, and the REPAIR interleave (one absolute slot from the
// full picture, epoch tag 63, every 32nd call while deltas flow — every
// call when idle) puts a hard ceiling on staleness. Word formats on COMM10:
//   0xFFFF                          seqlock invalidate
//   bit15=0: [14:9]=epoch [8:0]=slot     delta/repair slot (data in COMM6)
//   bit15=1: [14:9]=epoch [8:0]=count    end-of-pass marker (no data)
// Epochs count 0..62; 63 is the repair tag. On arm, the whole picture is
// queued as epoch 0's set, so the first paint is atomic like everything
// after it.
#define SMS_EPOCH_REPAIR 63
static uint8_t  sms_tile_epoch_on = 0;
static uint8_t  sms_epoch = 0;
static uint16_t sms_delta_q[SMS_TILE_WORDS];
static uint16_t sms_delta_n = 0;      // current epoch's set size
static uint16_t sms_delta_rot = 0;    // cursor over the set
static uint16_t sms_repair_rot = 0;   // cursor over the full picture
static uint16_t sms_post_pass_repair = 0;  // repair burst after each pass
// Z80 BUSREQ-PARK. The Z80 must never free-run (it hammers the PSG with
// whatever it executes — the build-302 clip), but it also must never be
// parked IN RESET outside a session boot pulse: the Z80 reset line resets
// the YM2612 with it, which is why the reset-hold parks kept killing the
// hum. Steady state everywhere outside a live SMS session is therefore
// reset HIGH + bus GRANTED AND HELD: the CPU is frozen, the Yamaha lives.
// The YM write paths run under the held grant and must NOT release it
// while this flag is set; the session boots clear it when they hand the
// bus back to the Z80. (Case 10's old "leaving it requested left the
// machine odd" was a request pending against a reset-held Z80 — a grant
// that never comes. A GRANTED hold is the normal 68K-owns-the-bus state.)
// NON-static: read_joypad (md_start.s) tests this to keep its RESUME_Z80
// from dissolving the park — the pad path toggles busreq every frame, and
// an unconditional release let the Z80 sprint between polls (the tick).
uint8_t z80_parked = 0;
static uint16_t sms_tile_rot = 0;
static uint8_t  sms_glass_on = 0;
static uint16_t sms_glass_rot = 0;
static uint16_t sms_glass_bits[SMS_GLASS_WORDS];

// It is recommended to put functions that run 1+ times every frame into RAM
// by specifying this attribute before the signature. This keeps the M68K off
// the ROM so the SH-2s can access it without slowdown.
// It should be safe to add or remove it from any function and experiment with
// the speed vs space differences

__attribute__((section(".data")))
void vdp_color(uint16_t index, uint16_t color) {
	index <<= 1;
	*vdp_ctrl_wide = ((0xC000 + (((uint32_t)index) & 0x3FFF)) << 16) + (((uint32_t)index) >> 14);
	*vdp_data_port = color;
}

// BUS A/B (TESTING>BUS on the 32X side; command 19 sets the flag). The main
// loop calls do_commands() as fast as a 7.67MHz 68K can spin, so *mars_comm0 --
// a 32X sysreg reached across the cart side -- is read every few microseconds,
// forever, against two SH-2s using the same bus. This is the only actor in the
// system whose poll rate nobody had throttled; both SH-2 sides got explicit
// throttles years apart (s_main.c's idle loop, raycast.c's barrier waits) and
// each time the symptom was the OTHER cpu starving.
//
// It CANNOT be a blind divider. Every sender in mars.c blocks on
// `while(MARS_SYS_COMM0)`, and HUD text is one command per tile, so a 1-in-N
// poll would just move the cost onto the primary and read as a loss.
// Adaptive backoff instead: the skip count grows by one per consecutive EMPTY
// poll up to BUS_POLL_GAP_MAX and collapses to zero the instant a command
// lands. An idle 68K settles at 1-in-16; a burst pays at most 15 loop
// iterations of latency ONCE, then runs at full rate for its whole length.
//
// The glass broadcast below stays outside this backoff on purpose: rationing it
// against COMM0 traffic would only desync the picture, and a glass session lives
// all of 8 frames with the tube filling your view, so there is nothing to save.
// Plain statics, NOT section(".data") like the functions above: that attribute
// exists to drag CODE off the cart and into 68K RAM, and marking data with it
// makes gcc reject the section as both executable and not ("section type
// conflict with sms_game_frame"). Ordinary globals already live in RAM here.
#define BUS_POLL_GAP_MAX 15
static uint16_t bus_throttle = 0;   // set by command 19
static uint16_t poll_gap = 0;       // current backoff, 0..BUS_POLL_GAP_MAX
static uint16_t poll_skip = 0;      // iterations still to skip

__attribute__((section(".data")))
void do_commands(void) {
	if (sms_glass_on) {                 // the glass broadcast rides every call
		// SEQLOCK, same as the tile branch below: invalidate the index
		// BEFORE moving the data, or the reader's check-read-recheck sees
		// a stale-valid index paired with the next slot's bits — on the
		// tube that race ate cells and read as extra "convergence tearing".
		uint16_t i = sms_glass_rot;
		*mars_comm10 = 0xFFFF;          // reader rejects: >= GLASS_WORDS
		*mars_comm6  = sms_glass_bits[i];
		*mars_comm10 = i;               // now publish
		sms_glass_rot = (uint16_t)((i + 1 >= SMS_GLASS_WORDS) ? 0 : i + 1);
	} else if (sms_tile_bcast && sms_tile_epoch_on) {
		// DIRTY-EPOCH stream, bandwidth inverted (the ghost-cell lesson):
		// ONE full pass of the delta set + marker, THEN a burst of repair
		// slots, repeat. The old 31-in-32 delta share re-sent a 3-cell set
		// hundreds of times a frame while repair crawled — but a dropped
		// cell's ONLY road home is repair (the cache-diff never re-sends
		// what it already holds), and the reader is away painting for a
		// chunk of every epoch's one-frame life. Redundant delta passes
		// are nearly worthless; repair bandwidth is priceless. Small sets
		// now give repair most of the wire (full-picture heal in under a
		// frame); big sets carry their own content. Idle: all repair.
		uint16_t slot; uint16_t tag;
		if (sms_delta_n && sms_post_pass_repair == 0) {
			slot = sms_delta_q[sms_delta_rot];
			tag  = (uint16_t)(((uint16_t)sms_epoch << 9) | slot);
		} else {
			slot = sms_repair_rot;
			tag  = (uint16_t)(((uint16_t)SMS_EPOCH_REPAIR << 9) | slot);
			sms_repair_rot = (uint16_t)
				((sms_repair_rot + 1 >= SMS_TILE_WORDS) ? 0 : sms_repair_rot + 1);
			if (sms_post_pass_repair) sms_post_pass_repair--;
		}
		const uint8_t *t = &sms_game_tiles[slot * 2u];
		*mars_comm10 = 0xFFFF;              // seqlock: invalidate first
		*mars_comm6  = (uint16_t)((uint16_t)t[0] | ((uint16_t)t[1] << 8));
		*mars_comm10 = tag;
		if (sms_delta_n && (tag >> 9) != SMS_EPOCH_REPAIR
		    && ++sms_delta_rot >= sms_delta_n) {
			sms_delta_rot = 0;
			// End-of-pass MARKER, then hand the wire to repair.
			*mars_comm10 = (uint16_t)(0x8000
				| ((uint16_t)sms_epoch << 9) | sms_delta_n);
			sms_post_pass_repair = 8;
		}
	} else if (sms_tile_bcast && ((++sms_tile_div & (SMS_TILE_DIV - 1)) == 0)) {
		// RATE-LIMITED. do_commands runs thousands of times a frame, so one
		// slot per call crossed the 384-slot picture many times over -- and
		// each crossing costs three 68K writes to 32X sysregs, which are slow
		// cart-side accesses. That matters because this function IS the vblank
		// poll: coarsen it enough and the 68K starts missing the flag
		// transitions, sms_game_frame fires at an erratic rate, FRAME_MBX
		// ticks wrong, and the Z80's PSG engine -- a phrase+echo with a
		// 21-frame delay, sms/DESIGN.md 4a -- loses its note-offs. A held note
		// is what a stuck PSG sounds like.
		//
		// One slot every SMS_TILE_DIV calls still crosses the whole picture
		// several times per frame, which is all the reader needs: the picture
		// only changes when the Z80 sets DIRTY, at most once a frame.
		//
		// SEQLOCK, not just a paired write. The reader checks the index,
		// reads the data, then re-checks the index -- which is only sound if
		// the index goes INVALID before the data moves. Publishing data-then-
		// index leaves a window where the index still reads valid and comm6
		// has already advanced, so slot i silently takes slot i+1's cells: a
		// 2-cell left shift through the whole picture. Invalidate first.
		uint16_t i = sms_tile_rot;
		const uint8_t *t = &sms_game_tiles[i * 2u];
		*mars_comm10 = 0xFFFF;          // reader rejects: >= SMS_TILE_WORDS
		*mars_comm6  = (uint16_t)((uint16_t)t[0] | ((uint16_t)t[1] << 8));
		*mars_comm10 = i;               // now publish
		sms_tile_rot = (uint16_t)((i + 1 >= SMS_TILE_WORDS) ? 0 : i + 1);
	}
	if (bus_throttle && poll_skip) { poll_skip--; return; }
	uint16_t cmd = *mars_comm0;
	if (bus_throttle) {
		if (cmd) {
			poll_gap = 0;               // burst: full rate until it goes quiet
		} else {
			if (poll_gap < BUS_POLL_GAP_MAX) poll_gap++;
			poll_skip = poll_gap;
		}
	}
	switch(cmd >> 8) {
	default: break; // Unknown command
	case 0: return; // No command
	case 3:
		*mars_comm8 = read_joypad(cmd);
		break;
	case 4: { // CLEAR SCREEN: sweep all of Name Table B. This case was EMPTY
		// from the day it was stubbed — HwMdClearScreen round-tripped to a
		// no-op, which never mattered until SMS32X needed plane B actually
		// empty (the glass handoff paints the attract card there, and with
		// the blit disarmed nothing ever overwrote it: the frozen white
		// title over the framebuffer picture). Name-table writes only —
		// the font in VRAM is never touched (grey-menus law).
		for (uint16_t row = 0; row < 28; row++) {
			uint32_t ofs = (row * 64u * 2u) + 0xE000;
			*vdp_ctrl_wide = (uint32_t)(0x4000 | (ofs & 0x3FFF)) << 16
			               | ((ofs >> 14) | 0x03);
			for (uint16_t i = 0; i < 40; i++)
				*vdp_data_port = 0;
		}
		break;
	}
	case 21: // FULLSCREEN-ON-32X: bit0 = broadcast tile ids (0 = blit to
		// plane B), bit1 = dirty-epoch protocol for the broadcast.
		sms_tile_bcast    = (uint8_t)(cmd & 1);
		sms_tile_epoch_on = (uint8_t)((cmd >> 1) & 1);
		sms_tile_rot = 0;
		sms_tile_div = 0;
		if (sms_tile_bcast && sms_tile_epoch_on) {
			// The whole picture IS epoch 0's set: the first paint arrives
			// through the same atomic path as every delta after it.
			for (uint16_t w = 0; w < SMS_TILE_WORDS; w++)
				sms_delta_q[w] = w;
			sms_delta_n   = SMS_TILE_WORDS;
			sms_delta_rot = 0;
			sms_epoch     = 0;
			sms_repair_rot = 0;
			sms_post_pass_repair = 0;
		}
		break;
	case 19: // BUS A/B: arm/disarm the idle COMM0 poll backoff (cmd low byte).
		bus_throttle = (uint16_t)(cmd & 0xFF);
		poll_gap = 0;                // both directions start at full rate
		poll_skip = 0;
		break;
	case 5: // Set VRAM or Plane offset
		vramOffset = *mars_comm2;
		break;
	case 6: // Write tile to Plane B
		*vdp_ctrl_wide = (((uint32_t)0x6000 + ((vramOffset) & 0x3FFF)) << 16) + (((vramOffset) >> 14) | 0x03);
		*vdp_data_port = *mars_comm2;
		vramOffset += 2;
		break;
	case 7: // Write word to VRAM address
		*vdp_ctrl_wide = (((uint32_t)0x4000 + ((vramOffset) & 0x3FFF)) << 16) + (((vramOffset) >> 14) | 0x00);
		*vdp_data_port = *mars_comm2;
		vramOffset += 2;
		break;
	case 8: // Set MD CRAM color: index in cmd low byte, BGR word in COMM2.
		// The SH-2 fade only reaches 32X CRAM; this is its handle on the
		// MD layer's colors (HUD text, backdrop) so fades take them too.
		vdp_color(cmd & 0xFF, *mars_comm2);
		break;
	case 9: { // SMS BOOT: Z80 heartbeat + the diag screen on the text layer.
		// Bus + reset dance: request the Z80 bus, hold reset, copy the
		// program into Z80 RAM through the $A00000 window, then release
		// reset and the bus — the Z80 wakes at $0000 inside SMS mode.
		volatile uint16_t *busreq = (uint16_t *)Z80_BUS_REQ;
		volatile uint16_t *reset  = (uint16_t *)Z80_RESET;
		volatile uint8_t  *zram   = (uint8_t *)0xA00000;
		// ORDER IS LAW (found by save-state forensics, 2026-08-09: nothing
		// ever landed in Z80 RAM): the MD never GRANTS the bus while the
		// Z80 is held in reset, so reset-then-request deadlocks the 68K in
		// the grant wait and the whole machine hangs politely on black.
		// Release reset FIRST, request, copy, then the stop-start dance.
		*reset = 0x100;                    // reset RELEASED or no grant ever
		*busreq = 0x100;                   // request the bus
		{ uint32_t g = 200000; while ((*busreq & 0x100) && --g) ; }
		for (uint16_t i = 0; i < Z80_SMS_HELLO_LEN; i++)
			zram[i] = z80_sms_hello[i];
		zram[Z80_SMS_MAILBOX_CMD]  = 0;    // the mailbox is OUTSIDE the payload
		zram[Z80_SMS_MAILBOX_DONE] = 0;    // and uninit Z80 RAM boots 0xFF —
		                                   // a phantom command fired a blind
		                                   // VDP burst that erased the font
		*reset = 0x000;                    // reset PULSE while we own the bus
		for (volatile uint16_t d = 0; d < 64; d++) ;   // let it latch
		*reset = 0x100;                    // release: PC parked at $0000, the
		                                   // HELD bus keeps the Z80 frozen —
		                                   // busreq-park, reset high, YM kept
		                                   // (the pulse wiped it; re-upload
		                                   // re-arms as everywhere else)
		z80_parked = 1;
		// v5: LOAD IN MODE 5, DISPLAY IN MODE 4. Slot-2 forensics closed
		// the book: in this emulator's mode 4, NO data-port write connects
		// — not the 68K's (magenta canary CRAM), not the Z80's (all 16
		// mailbox sections done, VRAM untouched). Registers work; data
		// does not. But VRAM is VRAM: fill it through the mode-5 pipes
		// the game proves every frame (font uploads, text tiles), THEN
		// flip M5 off. The Z80 keeps its heartbeat as the booted CPU.
		// v6: MODE 5 ALL THE WAY. Ares' MD VDP never implemented mode 4
		// (its own source: debug unimplemented "M5=0") — seven builds of
		// black screens were an emulator stub, not our hardware model.
		// The visible path is the one this cart proves every frame: the
		// mode-5 text layer. Draw the diag screen into Name Table B with
		// the boot font; the Z80 runs its heartbeat as the booted SMS
		// CPU. Mode-4 authenticity stays in git for a real-hardware day.
		{
			const unsigned char *t = z80_sms_text;
			while (*t != 0xFF) {
				uint16_t row = *t++;
				uint16_t col = *t++;
				uint16_t n   = *t++;
				uint32_t ofs = ((row * 64u + col) * 2u) + 0xE000;
				*vdp_ctrl_wide = (uint32_t)(0x4000 | (ofs & 0x3FFF)) << 16
				               | ((ofs >> 14) | 0x03);
				for (uint16_t i = 0; i < n; i++)
					*vdp_data_port = *t++;     // tile id, palette line 0
			}
		}
		break;
	}
	case 14: { // YM2612 part-I register write: reg in cmd low byte, value
		// in COMM2. The Yamaha sits on the Z80 bus, so take busreq for
		// the duration (the Z80's boot stub tolerates the pause), wait
		// out the YM busy flag before address AND data, release.
		volatile uint16_t *busreq = (uint16_t *)Z80_BUS_REQ;
		volatile uint8_t  *ym     = (uint8_t *)0xA04000;
		uint16_t val = *mars_comm2;
		// CANARY (menu/HUD text color names the failure in a screenshot):
		// GREEN = command arrived here. RED = Z80 bus grant timed out.
		// BLUE = grant OK but the YM busy flag never cleared ($A04000
		// reads garbage). Busy guards trimmed 10000->200: real busy is
		// ~32 chip cycles, and the old guards made every poisoned write
		// stall the 68K ~17ms — the CPU peg, and the REAL cause of the
		// B00245 1.2s hang (not vblank pacing).
		// (green arrival paint removed — hum confirmed audible; the
		// RED/BLUE error canaries below stay until the lab era ends)
		// ORDER IS LAW (the SMS boot's own lesson): no bus grant while
		// the Z80 is held in reset — and gameplay parks it in reset.
		// Release reset FIRST, then request; re-park on the way out.
		// The resident Z80 program runs a few harmless init opcodes in
		// the microseconds before the grant lands.
		volatile uint16_t *zreset = (uint16_t *)Z80_RESET;
		*zreset = 0x100;
		*busreq = 0x100;
		{ uint32_t g = 200000; while ((*busreq & 0x100) && --g) ;
		  if (!g) vdp_color(1, 0x00E); }
		{ uint32_t g = 200; while ((ym[0] & 0x80) && --g) ;
		  if (!g) vdp_color(1, 0xE00); }
		ym[0] = (uint8_t)(cmd & 0xFF);       // address port
		for (volatile uint16_t d = 0; d < 8; d++) ;
		{ uint32_t g = 200; while ((ym[0] & 0x80) && --g) ; }
		ym[1] = (uint8_t)val;                // data port
		for (volatile uint16_t d = 0; d < 32; d++) ;
		{ uint32_t g = 200; while ((ym[0] & 0x80) && --g) ; }
		// Release ONLY if a session owns the Z80: the busreq-park IS the
		// held grant, and dropping it here is what un-parked the chip and
		// let it free-run into the PSG (the build-302 clip). Reset stays
		// high throughout, so the YM keeps its registers either way.
		if (!z80_parked) *busreq = 0x000;
		break;
	}
	case 15: { // YM hum control, ONE command per action (the per-register
		// path costs a frame per write — a 70-write patch upload hung
		// the primary for ~1.2s). op in cmd low byte: 0 = all off,
		// 1 = upload both hum patches + key bed on, 2 = sting key-on,
		// 3 = sting release. Patch mirror of sh_src/menu.c ym_hum_set.
		static const uint8_t hum_patch[][2] = {
			// LFO OFF (was 0x08, enabled at ~4Hz). With the sample buzz
			// retired, the LFO's amplitude wobble on the bed played naked
			// and read as tick-tick-tick — THE build-302 "clipping" (the
			// Z80 was innocent; it is parked and frozen now regardless).
			// The bed runs flat until the FM lab retunes a subtle wobble
			// via FMS instead of AM. (The menu.c ym_hum_set mirror this
			// table's comment referenced no longer exists.)
			{0x22, 0x00}, {0x27, 0x00}, {0x2B, 0x00},
			// ch1: neon sting
			{0x30, 0x01}, {0x34, 0x03}, {0x38, 0x02}, {0x3C, 0x14},
			{0x40, 0x28}, {0x44, 0x34}, {0x48, 0x20}, {0x4C, 0x3A},
			{0x50, 0x1F}, {0x54, 0x1F}, {0x58, 0x1F}, {0x5C, 0x1F},
			{0x60, 0x80}, {0x64, 0x80}, {0x68, 0x80}, {0x6C, 0x80},
			{0x70, 0x00}, {0x74, 0x00}, {0x78, 0x00}, {0x7C, 0x00},
			{0x80, 0x06}, {0x84, 0x06}, {0x88, 0x06}, {0x8C, 0x06},
			{0x90, 0x00}, {0x94, 0x00}, {0x98, 0x00}, {0x9C, 0x00},
			{0xB0, 0x2F}, {0xB4, 0xD1}, {0xA4, 0x0C}, {0xA0, 0x9D},
			// ch2: buzz bed
			{0x31, 0x01}, {0x35, 0x03}, {0x39, 0x02}, {0x3D, 0x34},
			{0x41, 0x30}, {0x45, 0x3C}, {0x49, 0x28}, {0x4D, 0x42},
			{0x51, 0x1F}, {0x55, 0x1F}, {0x59, 0x1F}, {0x5D, 0x1F},
			{0x61, 0x80}, {0x65, 0x80}, {0x69, 0x80}, {0x6D, 0x80},
			{0x71, 0x00}, {0x75, 0x00}, {0x79, 0x00}, {0x7D, 0x00},
			{0x81, 0x08}, {0x85, 0x08}, {0x89, 0x08}, {0x8D, 0x08},
			{0x91, 0x00}, {0x95, 0x00}, {0x99, 0x00}, {0x9D, 0x00},
			{0xB1, 0x3F}, {0xB5, 0xE1}, {0xA5, 0x0C}, {0xA1, 0x9D},
		};
		volatile uint16_t *busreq = (uint16_t *)Z80_BUS_REQ;
		volatile uint16_t *zreset = (uint16_t *)Z80_RESET;
		volatile uint8_t  *ym     = (uint8_t *)0xA04000;
		uint16_t op = cmd & 0xFF;
		*zreset = 0x100;                     // no grant while reset (LAW)
		*busreq = 0x100;
		{ uint32_t g = 200000; while ((*busreq & 0x100) && --g) ; }
		// Busy-poll PLUS fixed settle delays: the real chip DROPS writes
		// that land while it is busy, and if the status read lies (open
		// bus, emulator shortcut) the poll passes instantly — B00246's
		// back-to-back burst was silent while B00245's frame-spaced
		// writes (same values!) sounded. The delays are the classic
		// pacing (address ~17 68K cycles, data ~83) with margin; the
		// whole 70-write patch still lands in ~2ms.
		#define YMW(r, v) do { \
			uint32_t g = 200; while ((ym[0] & 0x80) && --g) ; \
			ym[0] = (r); \
			for (volatile uint16_t d = 0; d < 8; d++) ; \
			g = 200; while ((ym[0] & 0x80) && --g) ; \
			ym[1] = (v); \
			for (volatile uint16_t d = 0; d < 32; d++) ; } while (0)
		switch (op) {
		case 0: YMW(0x28, 0x00); YMW(0x28, 0x01); break;
		case 1:
			for (uint16_t i = 0; i < sizeof(hum_patch) / 2; i++)
				YMW(hum_patch[i][0], hum_patch[i][1]);
			YMW(0x28, 0xF1);                       // bed on (ch2)
			break;
		case 2: YMW(0x28, 0x00); YMW(0x28, 0xF0); break;  // sting retrigger
		case 3: YMW(0x28, 0x00); break;                   // sting release
		}
		#undef YMW
		{ uint32_t g = 200; while ((ym[0] & 0x80) && --g) ; }
		// Same rule as case 14: the held grant IS the park; only a live
		// session releases the bus. Reset high, YM registers survive.
		if (!z80_parked) *busreq = 0x000;
		break;
	}
	case 10: { // SMS STOP: sweep the splash rows, park the Z80. NOTHING MORE.
		// The old "full restore" tail (InitVDPRegs replay + CRAM repaint +
		// $3800 sweep) was the grey-menu FONT-ERASER: two bisect rounds
		// proved every exit that skipped it kept the font, and the wipe's
		// forensic footprint was exactly the boot font loop's 1440 bytes.
		// Mechanism never named — but in the mode-5-only design the tail
		// restored state the boot never touches, so it is deleted, not
		// gated. If a future mode-4 (real hardware) arc resurrects a
		// restore path, THIS is the first suspect when menus go grey.
		{
			const unsigned char *t = z80_sms_text;
			while (*t != 0xFF) {
				uint16_t row = *t++;
				uint16_t col = *t++;
				uint16_t n   = *t++;
				t += n;
				uint32_t ofs = ((row * 64u + col) * 2u) + 0xE000;
				*vdp_ctrl_wide = (uint32_t)(0x4000 | (ofs & 0x3FFF)) << 16
				               | ((ofs >> 14) | 0x03);
				for (uint16_t i = 0; i < n; i++)
					*vdp_data_port = 0;
			}
		}
		volatile uint16_t *busreq = (uint16_t *)Z80_BUS_REQ;
		// BUSREQ-park, not reset-park (see z80_parked): reset stays HIGH so
		// the YM keeps its registers, the held grant freezes the CPU. The
		// old "leaving it requested left the machine odd" was a pending
		// request against a reset-held Z80; this grant is taken while the
		// chip runs, which is the ordinary 68K-owns-the-bus state.
		*busreq = 0x100;
		{ uint32_t g = 200000; while ((*busreq & 0x100) && --g) ; }
		z80_parked = 1;
		break;
	}
	case 11: { // SMS GAME MAP: stage one word of the level patch (index in
		// the command's low byte, 0..65). Stateless per word, so a dropped
		// or repeated command can't shear the whole map.
		uint16_t idx = cmd & 0xFF;
		if (idx < Z80_GAME_MAP_LEN / 2) {
			uint16_t w = *mars_comm2;
			sms_game_map[idx * 2]     = (uint8_t)(w >> 8);
			sms_game_map[idx * 2 + 1] = (uint8_t)w;
		}
		break;
	}
	case 12: { // SMS GAME BOOT: upload the mini-game, patch the staged map
		// in, and — unlike the diag spike, whose payload is vestigial in
		// the mode-5 design — actually RUN the Z80. It is the game CPU.
		volatile uint16_t *busreq = (uint16_t *)Z80_BUS_REQ;
		volatile uint16_t *reset  = (uint16_t *)Z80_RESET;
		volatile uint8_t  *zram   = (uint8_t *)0xA00000;
		// Sweep the visible plane rows first so the playfield starts clean
		// (menu/HUD tiles persist otherwise). Name-table writes only — the
		// font in VRAM is never touched (grey-menus law).
		for (uint16_t row = 0; row < 28; row++) {
			uint32_t ofs = (row * 64u * 2u) + 0xE000;
			*vdp_ctrl_wide = (uint32_t)(0x4000 | (ofs & 0x3FFF)) << 16
			               | ((ofs >> 14) | 0x03);
			for (uint16_t i = 0; i < 40; i++)
				*vdp_data_port = 0;
		}
		// ORDER IS LAW (see the SMS BOOT case): release reset BEFORE the
		// bus request or the grant never comes.
		*reset = 0x100;
		*busreq = 0x100;
		{ uint32_t g = 200000; while ((*busreq & 0x100) && --g) ; }
		for (uint16_t i = 0; i < Z80_GAME_LEN; i++)
			zram[i] = z80_sms_game[i];
		// the pack's three ranges land at three addresses — the game's
		// vars live in the gaps, a straight 440B copy would flatten them
		for (uint16_t i = 0; i < Z80_GAME_MAP_CORE; i++)
			zram[Z80_GAME_MAP_BITS + i] = sms_game_map[i];
		for (uint16_t i = 0; i < Z80_GAME_PEDGE_N_LEN; i++)
			zram[Z80_GAME_PEDGE_N + i] =
				sms_game_map[Z80_GAME_MAP_CORE + i];
		for (uint16_t i = 0; i < Z80_GAME_PEDGE_W_LEN; i++)
			zram[Z80_GAME_PEDGE_W + i] =
				sms_game_map[Z80_GAME_MAP_CORE + Z80_GAME_PEDGE_N_LEN + i];
		zram[Z80_GAME_PAD_MBX]   = 0;      // mailboxes live OUTSIDE the
		zram[Z80_GAME_DIRTY_MBX] = 0;      // payload and uninit Z80 RAM
		zram[Z80_GAME_STATE_MBX] = 0;      // boots 0xFF (the phantom-
		zram[Z80_GAME_FRAME_MBX] = 0;      // mailbox-command lesson)
		zram[Z80_GAME_HEART]     = 0;
		{	// PSG silent before the Z80 takes it (68K reaches it at $C00011)
			volatile uint8_t *psg = (uint8_t *)0xC00011;
			*psg = 0x9F; *psg = 0xBF; *psg = 0xDF; *psg = 0xFF;
		}
		*reset = 0x000;                    // reset pulse while we own the bus
		for (volatile uint16_t d = 0; d < 64; d++) ;   // let it latch
		*busreq = 0x000;                   // hand the bus back...
		*reset = 0x100;                    // ...and let the Z80 run from $0000
		sms_game_on = 1;
		sms_glass_on = 0;                  // modal takes the one Z80 over
		z80_parked = 0;                    // the session owns the bus now
		break;
	}
	case 16: { // SMS GLASS BOOT: same upload + map patch + run, but
		// HEADLESS — no name-table sweep or blit (the MD text layer
		// belongs to the 3D HUD), pad held at 0 so the game sits on its
		// TEST PATTERN attract card, and the COMM6/COMM10 broadcast
		// starts so the SH-2 can paint TILEBUF onto the PVM glass.
		volatile uint16_t *busreq = (uint16_t *)Z80_BUS_REQ;
		volatile uint16_t *reset  = (uint16_t *)Z80_RESET;
		volatile uint8_t  *zram   = (uint8_t *)0xA00000;
		*reset = 0x100;                    // ORDER IS LAW (see SMS BOOT)
		*busreq = 0x100;
		{ uint32_t g = 200000; while ((*busreq & 0x100) && --g) ; }
		for (uint16_t i = 0; i < Z80_GAME_LEN; i++)
			zram[i] = z80_sms_game[i];
		// the pack's three ranges land at three addresses — the game's
		// vars live in the gaps, a straight 440B copy would flatten them
		for (uint16_t i = 0; i < Z80_GAME_MAP_CORE; i++)
			zram[Z80_GAME_MAP_BITS + i] = sms_game_map[i];
		for (uint16_t i = 0; i < Z80_GAME_PEDGE_N_LEN; i++)
			zram[Z80_GAME_PEDGE_N + i] =
				sms_game_map[Z80_GAME_MAP_CORE + i];
		for (uint16_t i = 0; i < Z80_GAME_PEDGE_W_LEN; i++)
			zram[Z80_GAME_PEDGE_W + i] =
				sms_game_map[Z80_GAME_MAP_CORE + Z80_GAME_PEDGE_N_LEN + i];
		zram[Z80_GAME_PAD_MBX]   = 0;
		zram[Z80_GAME_DIRTY_MBX] = 0;
		zram[Z80_GAME_STATE_MBX] = 0;
		zram[Z80_GAME_FRAME_MBX] = 0;
		zram[Z80_GAME_HEART]     = 0;
		{	// PSG silent before the Z80 takes it — its chime + music then
			// play IN-WORLD under the PWM mix (the monitor sings, 4c)
			volatile uint8_t *psg = (uint8_t *)0xC00011;
			*psg = 0x9F; *psg = 0xBF; *psg = 0xDF; *psg = 0xFF;
		}
		for (uint16_t i = 0; i < Z80_GAME_TILEBUF_LEN; i++)
			sms_game_tiles[i] = 0;         // broadcast starts dark, not stale
		for (uint16_t i = 0; i < SMS_GLASS_WORDS; i++)
			sms_glass_bits[i] = 0;
		*reset = 0x000;
		for (volatile uint16_t d = 0; d < 64; d++) ;
		*busreq = 0x000;
		*reset = 0x100;
		sms_glass_on = 1;
		sms_glass_rot = 0;
		sms_game_on = 0;
		z80_parked = 0;                    // the glass session owns the bus now
		break;
	}
	case 18: { // GLASS -> FULLSCREEN handoff. The SAME Z80 keeps running:
		// no reboot, no second chime, the music carries across the cut.
		// Sweep the plane the modal blit owns, paint the cached frame
		// once (the Z80 only sets DIRTY when its picture CHANGES, and
		// sitting on the attract card it changes nothing — without this
		// the fullscreen would open black), then flip the flags so the
		// next frame feeds the real pad.
		//
		// SMS32X armed: skip the sweep AND the bridge paint. The SH-2 is
		// keeping its last world frame up as the bridge and will clear
		// plane B itself — painting the attract card here would flash
		// white MD text over that frame for the frames until the clear.
		if (sms_tile_bcast) {
			sms_glass_on = 0;
			sms_game_on  = 1;
			break;
		}
		for (uint16_t row = 0; row < 28; row++) {
			uint32_t ofs = (row * 64u * 2u) + 0xE000;
			*vdp_ctrl_wide = (uint32_t)(0x4000 | (ofs & 0x3FFF)) << 16
			               | ((ofs >> 14) | 0x03);
			for (uint16_t i = 0; i < 40; i++)
				*vdp_data_port = 0;
		}
		{
			const uint8_t *t = sms_game_tiles;
			for (uint16_t row = 0; row < Z80_GAME_TILEBUF_ROWS; row++) {
				uint32_t ofs = (((row + 2u) * 64u + 4u) * 2u) + 0xE000;
				*vdp_ctrl_wide = (uint32_t)(0x4000 | (ofs & 0x3FFF)) << 16
				               | ((ofs >> 14) | 0x03);
				for (uint16_t i = 0; i < Z80_GAME_TILEBUF_COLS; i++) {
					uint16_t id = *t++;
					// art tiles (128+) exist only on the SMS32X arm;
					// this plane has just the boot font in VRAM
					*vdp_data_port = (id & 0x80) ? 44 : id;
				}
			}
		}
		sms_glass_on = 0;
		sms_game_on  = 1;
		break;
	}
	case 17: { // SMS GLASS STOP: park the Z80, silence the PSG. No
		// name-table sweep — the glass session never touched it.
		volatile uint16_t *busreq = (uint16_t *)Z80_BUS_REQ;
		sms_glass_on = 0;
		// BUSREQ-park (see z80_parked): reset high, YM survives, CPU held.
		*busreq = 0x100;
		{ uint32_t g = 200000; while ((*busreq & 0x100) && --g) ; }
		z80_parked = 1;
		{
			volatile uint8_t *psg = (uint8_t *)0xC00011;
			*psg = 0x9F; *psg = 0xBF; *psg = 0xDF; *psg = 0xFF;
		}
		break;
	}
	case 13: { // SMS GAME STOP: park the Z80, sweep the playfield rows.
		volatile uint16_t *busreq = (uint16_t *)Z80_BUS_REQ;
		sms_game_on = 0;
		// BUSREQ-park (see z80_parked): frozen mid-program, reset HIGH so
		// the hum's YM registers survive the session. The next boot's
		// reset pulse gives the game a clean start.
		*busreq = 0x100;                   // bounded — see SMS STOP
		{ uint32_t g = 200000; while ((*busreq & 0x100) && --g) ; }
		z80_parked = 1;
		{	// the Z80 died mid-note: silence the PSG (teardown rule, 4a)
			volatile uint8_t *psg = (uint8_t *)0xC00011;
			*psg = 0x9F; *psg = 0xBF; *psg = 0xDF; *psg = 0xFF;
		}
		for (uint16_t row = 2; row < 26; row++) {
			uint32_t ofs = ((row * 64u + 4u) * 2u) + 0xE000;
			*vdp_ctrl_wide = (uint32_t)(0x4000 | (ofs & 0x3FFF)) << 16
			               | ((ofs >> 14) | 0x03);
			for (uint16_t i = 0; i < 32; i++)
				*vdp_data_port = 0;
		}
		break;
	}
	}
	*mars_comm0 = 0;
}

// One 68K frame of the SMS mini-game duet: under a brief bus request, hand
// the Z80 the pad byte and a frame tick; if it flagged a fresh frame, pull
// TILEBUF out and blit it to the text layer (rows 2..25, cols 4..35 — the
// 32-wide field centered in the 40-column plane). The Z80 never touches the
// VDP; the 68K never touches game state. The bus pause is a few dozen
// microseconds — the same trick every sound driver on the platform uses.
// modal: real pad in, TILEBUF blitted to the text layer. glass (headless):
// pad held 0 so the Z80 sits on its attract card, no blit — the picture
// leaves through the COMM broadcast instead.
__attribute__((section(".data")))
static void sms_game_frame(uint8_t modal) {
	volatile uint16_t *busreq = (uint16_t *)Z80_BUS_REQ;
	volatile uint8_t  *zram   = (uint8_t *)0xA00000;
	uint8_t pad = modal ? (uint8_t)*mars_comm8 : 0;  // U1 D2 L4 R8 B10 C20 A40 ST80
	uint8_t dirty;
	*busreq = 0x100;
	{ uint32_t g = 100000; while ((*busreq & 0x100) && --g) ; }
	zram[Z80_GAME_PAD_MBX] = pad;
	zram[Z80_GAME_FRAME_MBX] = (uint8_t)(zram[Z80_GAME_FRAME_MBX] + 1);
	// START-ownership flag, stateless: while STATE_MBX==1 (debrief OR the
	// diagnostics page) the SH-2 lets START through to the Z80 (which
	// returns to its terminal) instead of exiting the session. COMM8 b14.
	sms_debrief = (uint8_t)(modal && zram[Z80_GAME_STATE_MBX] == 1);
	if (modal) {
		// maze status for the SH-2's SMOOTH renderer: player cell +
		// maze-active, published every frame (COMM14 is otherwise unused)
		uint16_t st = 0x8000
			| ((zram[Z80_GAME_VAR_GSTATE] == 1
			    && zram[Z80_GAME_STATE_MBX] == 0) ? 0x0400 : 0)
			| ((uint16_t)(zram[Z80_GAME_VAR_PX] & 31) << 5)
			| (zram[Z80_GAME_VAR_PY] & 31);
		*((volatile uint16_t *)MARS_COMM14) = st;
	}
	dirty = zram[Z80_GAME_DIRTY_MBX];
	if (dirty) {
		if (sms_tile_bcast && sms_tile_epoch_on) {
			// DIRTY-EPOCH: diff against the cache while copying and queue
			// only the changed words as the next epoch's set. Payload
			// stays absolute, so this is a filter, never a dependency.
			uint16_t n = 0;
			for (uint16_t w = 0; w < Z80_GAME_TILEBUF_LEN / 2; w++) {
				uint8_t a = zram[Z80_GAME_TILEBUF + w * 2u];
				uint8_t b = zram[Z80_GAME_TILEBUF + w * 2u + 1];
				if (a != sms_game_tiles[w * 2u] || b != sms_game_tiles[w * 2u + 1]) {
					sms_game_tiles[w * 2u]     = a;
					sms_game_tiles[w * 2u + 1] = b;
					sms_delta_q[n++] = w;
				}
			}
			if (n) {
				sms_delta_rot = 0;
				sms_delta_n   = n;
				sms_epoch = (uint8_t)((sms_epoch + 1) % SMS_EPOCH_REPAIR);
			}
		} else {
			for (uint16_t i = 0; i < Z80_GAME_TILEBUF_LEN; i++)
				sms_game_tiles[i] = zram[Z80_GAME_TILEBUF + i];
		}
		zram[Z80_GAME_DIRTY_MBX] = 0;
	}
	*busreq = 0x000;
	if (dirty && !modal) {           // rebake the glass occupancy bitmap
		const uint8_t *t = sms_game_tiles;
		for (uint16_t w = 0; w < SMS_GLASS_WORDS; w++) {
			uint16_t bits = 0;
			for (uint16_t b = 0; b < 16; b++)
				if (*t++) bits |= (uint16_t)(1u << b);
			sms_glass_bits[w] = bits;
		}
	}
	if (dirty && modal && !sms_tile_bcast) {
		const uint8_t *t = sms_game_tiles;
		for (uint16_t row = 0; row < Z80_GAME_TILEBUF_ROWS; row++) {
			uint32_t ofs = (((row + 2u) * 64u + 4u) * 2u) + 0xE000;
			*vdp_ctrl_wide = (uint32_t)(0x4000 | (ofs & 0x3FFF)) << 16
			               | ((ofs >> 14) | 0x03);
			for (uint16_t i = 0; i < Z80_GAME_TILEBUF_COLS; i++) {
				uint16_t id = *t++;
				// art tiles (128+) exist only on the SMS32X arm
				*vdp_data_port = (id & 0x80) ? 44 : id;
			}
		}
	}
}

// Sticky six-button latch. read_joypad returns bit 0x1000 set when the pad
// validated the six-button signature THIS frame, with M X Y Z in 0x0F00.
// Wireless receivers/adapters validate intermittently (async latching vs our
// TH probe), and a miss used to drop MODE mid-hold — which also defeats the
// SH-2's MODE-held-2-frames debounce. Once a pad has EVER validated, a miss
// holds the last good extended bits instead. The failed frame's own extended
// data is never trusted (that way lie the phantoms).
__attribute__((section(".data")))
uint16_t pad_sticky(uint8_t n, uint16_t p) {
	static uint16_t last_ext[2];
	static uint8_t  is_six[2];
	if (p & 0x1000) {
		is_six[n] = 1;
		last_ext[n] = p & 0x0F00;
	} else if (is_six[n]) {
		p = (uint16_t)((p & ~0x0F00u) | last_ext[n] | 0x1000);
	}
	return p;
}

__attribute__((section(".data")))
void main(void) {
	// PARK THE Z80 AND THE PSG, in that order — and park with BUSREQ, not
	// reset. Attenuating the PSG once is not enough: a free-running Z80
	// keeps hitting the sound chip (the build-302 clip). But a reset-hold
	// park resets the YM2612 with it AND the YM command paths release
	// reset anyway, dissolving the park seconds after boot. So: request
	// the bus first (pending — no grant comes while reset is low), then
	// release reset; the Z80 executes at most an instruction or two of
	// zeroed RAM before the grant freezes it, reset stays high, the
	// Yamaha lives, and the held grant IS the park.
	{
		volatile uint16_t *busreq = (uint16_t *)Z80_BUS_REQ;
		volatile uint16_t *reset  = (uint16_t *)Z80_RESET;
		*busreq = 0x100;                   // pending until reset releases
		*reset  = 0x100;
		{ uint32_t g = 200000; while ((*busreq & 0x100) && --g) ; }
		z80_parked = 1;                    // bus stays held: frozen, not reset
	}
	// The teardown rule from sms/DESIGN.md 4a applied at startup too:
	// attenuation 15 on all four channels, before anything else runs.
	{
		volatile uint8_t *psg = (uint8_t *)0xC00011;
		*psg = 0x9F; *psg = 0xBF; *psg = 0xDF; *psg = 0xFF;
	}
	// Backdrop/border stays BLACK. The marsdev demo's grey color-cycle
	// lived here for the project's whole life: Ares crops the overscan so
	// nobody saw it, but a CRT shows the border pulsing grey at full
	// brightness through every dark scene and fade. Boot already set CRAM
	// 0 black; nothing may write it again.
	while(1) {
		// TODO: Remove this after fixing _vblank
		if (sms_active) {
			// mode 4: the mode-5 vblank bit is dead — pace by delay,
			// keep serving commands, and FALL THROUGH to the pad publish.
			for (volatile uint16_t dl = 0; dl < 2500; dl++) do_commands();
		} else {
			while(*vdp_ctrl_port & 8) do_commands();
			while(!(*vdp_ctrl_port & 8)) do_commands();
		}
		// Publish both pads UNSOLICITED every frame, exactly like the COMM12
		// frame tick below. The SH-2 then reads COMM8/COMM10 directly with no
		// request/response round-trip — the old on-demand handshake (COMM0
		// command 3, still serviced above for compatibility) is what starved
		// the bridge under render contention. P2 is published for free; no
		// gameplay reads COMM10 yet.
		{
			// COMM8 bit 15 is provably free (read_joypad masks to 13
			// bits): it carries the Z80 terminal's DIAGNOSTIC request
			// (STATE_MBX=2, latched by sms_game_frame) up to the SH-2,
			// which tears the game session down and boots the diag.
			uint16_t p = pad_sticky(0, read_joypad(0));
			if (sms_debrief) p |= 0x4000;
			*mars_comm8 = p;
		}
		if (!sms_glass_on && !sms_tile_bcast)   // both broadcasts borrow COMM10
			*mars_comm10 = pad_sticky(1, read_joypad(1));
		*mars_comm12 = ++timer;
		if (sms_game_on)       sms_game_frame(1);
		else if (sms_glass_on) sms_game_frame(0);
	}
}
