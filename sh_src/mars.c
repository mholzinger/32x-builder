#include "mars.h"
#include "string.h"

static int X = 0, Y = 0;
static int MX = 40, MY = 25;
static int init = 0;
static uint16_t fgc = 0, bgc = 0;
static uint8_t fgs = 0, bgs = 0;

static uint16_t currentFB = 0;

void Hw32xSetFGColor(int s, int r, int g, int b) {
	volatile uint16_t *palette = &MARS_CRAM;
	fgs = s;
	fgc = COLOR(r, g, b);
	palette[fgs] = fgc;
}

void Hw32xSetBGColor(int s, int r, int g, int b) {
	volatile uint16_t *palette = &MARS_CRAM;
	bgs = s;
	bgc = COLOR(r, g, b);
	palette[bgs] = bgc;
}

void Hw32xInit(int vmode, int lineskip) {
	volatile uint16_t *frameBuffer16 = &MARS_FRAMEBUFFER;

	// Wait for the SH2 to gain access to the VDP
	while((MARS_SYS_INTMSK & MARS_SH2_ACCESS_VDP) == 0);

	if(vmode == MARS_VDP_MODE_256) {
		// Set 8-bit paletted mode, 224 lines
		MARS_VDP_DISPMODE = MARS_224_LINES | MARS_VDP_MODE_256;

		// Flip the framebuffer selection bit and wait for it to take effect
		MARS_VDP_FBCTL = currentFB ^ 1;
		while((MARS_VDP_FBCTL & MARS_VDP_FS) == currentFB) ;
		currentFB ^= 1;
		// rewrite line table
		for(int i=0; i<224/(lineskip+1); i++) {
			int j = lineskip + 1;
			while(j) {
				frameBuffer16[i*(lineskip+1) + (lineskip + 1 - j)] = i*160 + 0x100; /* word offset of line */
				j--;
			}
		}
		// clear screen
		for(int i=0x100; i<0x10000; i++) frameBuffer16[i] = 0;

		// Flip the framebuffer selection bit and wait for it to take effect
		MARS_VDP_FBCTL = currentFB ^ 1;
		while((MARS_VDP_FBCTL & MARS_VDP_FS) == currentFB) ;
		currentFB ^= 1;
		// rewrite line table
		for(int i=0; i<224/(lineskip+1); i++) {
			int j = lineskip + 1;
			while(j) {
				frameBuffer16[i*(lineskip+1) + (lineskip + 1 - j)] = i*160 + 0x100; /* word offset of line */
				j--;
			}
		}
		// clear screen
		for(int i=0x100; i<0x10000; i++) frameBuffer16[i] = 0;

		MX = 40;
		MY = 28/(lineskip+1);
	} else if(vmode == MARS_VDP_MODE_32K) {
		// Set 16-bit direct mode, 224 lines
		MARS_VDP_DISPMODE = MARS_224_LINES | MARS_VDP_MODE_32K;

		// Flip the framebuffer selection bit and wait for it to take effect
		MARS_VDP_FBCTL = currentFB ^ 1;
		while ((MARS_VDP_FBCTL & MARS_VDP_FS) == currentFB) ;
		currentFB ^= 1;
		// rewrite line table
		for(int i=0; i<224/(lineskip+1); i++) {
			if (lineskip) {
				int j = lineskip + 1;
				while (j)  {
					frameBuffer16[i*(lineskip+1) + (lineskip + 1 - j)] = i*320 + 0x100; /* word offset of line */
					j--;
				}
			} else {
				if(i<200) frameBuffer16[i] = i*320 + 0x100; /* word offset of line */
				else frameBuffer16[i] = 200*320 + 0x100;    /* word offset of line */
			}
		}
		// clear screen
		for(int i=0x100; i<0x10000; i++) frameBuffer16[i] = 0;

		// Flip the framebuffer selection bit and wait for it to take effect
		MARS_VDP_FBCTL = currentFB ^ 1;
		while((MARS_VDP_FBCTL & MARS_VDP_FS) == currentFB) ;
		currentFB ^= 1;
		// rewrite line table
		for(int i=0; i<224/(lineskip+1); i++) {
			if (lineskip) {
				int j = lineskip + 1;
				while (j) {
					frameBuffer16[i*(lineskip+1) + (lineskip + 1 - j)] = i*320 + 0x100; /* word offset of line */
					j--;
				}
			} else {
				if(i<200) frameBuffer16[i] = i*320 + 0x100; /* word offset of line */
				else frameBuffer16[i] = 200*320 + 0x100; /* word offset of line */
			}
		}
		// clear screen
		for(int i=0x100; i<0x10000; i++) frameBuffer16[i] = 0;

		MX = 40;
		MY = 25/(lineskip+1);
	}

	Hw32xSetFGColor(255,31,31,31);
	Hw32xSetBGColor(0,0,0,0);
	X = Y = 0;
	init = vmode;
}

int Hw32xScreenGetX(void) {
	return X;
}

int Hw32xScreenGetY(void) {
	return Y;
}

void Hw32xScreenSetXY(int x, int y) {
	if(x<MX && x>=0) X = x;
	if(y<MY && y>=0) Y = y;
}

void Hw32xScreenClear(void) {
	int l = (init == MARS_VDP_MODE_256) ? 320*224/2 + 0x100 : 320*200 + 0x100;
	volatile unsigned short *frameBuffer16 = &MARS_FRAMEBUFFER;

	// clear screen
	for(int i=0x100; i<l; i++) frameBuffer16[i] = 0;

	// Flip the framebuffer selection bit and wait for it to take effect
	MARS_VDP_FBCTL = currentFB ^ 1;
	while((MARS_VDP_FBCTL & MARS_VDP_FS) == currentFB) ;
	currentFB ^= 1;

	// clear screen
	for(int i=0x100; i<l; i++) frameBuffer16[i] = 0;

	Hw32xSetFGColor(255,31,31,31);
	Hw32xSetBGColor(0,0,0,0);
	X = Y = 0;
}

void Hw32xDelay(int ticks) {
	unsigned long ct = MARS_SYS_COMM12 + ticks;
	while(MARS_SYS_COMM12 < ct) ;
}

void Hw32xScreenFlip(int wait) {
	// Flip the framebuffer selection bit
	MARS_VDP_FBCTL = currentFB ^ 1;
	if(wait) {
		while((MARS_VDP_FBCTL & MARS_VDP_FS) == currentFB) ;
		currentFB ^= 1;
	}
}

void Hw32xFlipWait(void) {
	while ((MARS_VDP_FBCTL & MARS_VDP_FS) == currentFB) ;
	currentFB ^= 1;
}


// MD Command support code ---------------------------------------------

void HwMdReadPad(uint8_t port) {
	/* No-op. The 68K now publishes both pads to COMM8/COMM10 every frame
	 * unsolicited (md_main.c), so the value is already fresh — callers read
	 * MARS_SYS_COMM8 / MARS_SYS_COMM10 directly. Kept so existing call sites
	 * compile unchanged. The old body was a blocking COMM0 request/response
	 * round-trip (spin, write 0x0300|port, spin until the 68K clears it): the
	 * primary stalled on it every frame, and its COMM traffic during render
	 * collided with the 68K's servicing window — the "joypad bridge" that
	 * starved under contention and sank the work-stealing experiment. */
	(void)port;
}

void HwMdClearScreen(void) {
	while(MARS_SYS_COMM0) ; // wait until 68000 has responded to any earlier requests
	MARS_SYS_COMM0 = 0x0400; // Clear Screen (Name Table B)
	while(MARS_SYS_COMM0) ;
}

void HwMdSetOffset(unsigned short offset) {
	while(MARS_SYS_COMM0) ; // wait until 68000 has responded to any earlier requests
	MARS_SYS_COMM2 = offset;
	MARS_SYS_COMM0 = 0x0500; // Set offset (into either Name Table B or VRAM)
	while(MARS_SYS_COMM0) ;
}

void HwMdSetNTable(unsigned short word) {
	while(MARS_SYS_COMM0) ; // wait until 68000 has responded to any earlier requests
	MARS_SYS_COMM2 = word;
	MARS_SYS_COMM0 = 0x0600; // Set word at offset in Name Table B
	while(MARS_SYS_COMM0) ;
}

void HwMdSetVram(unsigned short word) {
	while(MARS_SYS_COMM0) ; // wait until 68000 has responded to any earlier requests
	MARS_SYS_COMM2 = word;
	MARS_SYS_COMM0 = 0x0700; // Set word at offset in VRAM
	while(MARS_SYS_COMM0) ;
}

/* BUS A/B (TESTING>BUS): tell the 68K whether to back its COMM0 poll off when
 * idle. The 68K's main loop calls do_commands() as fast as it can spin, so it
 * reads a 32X sysreg every few microseconds forever — the one actor in the
 * system whose bus appetite nobody had throttled. It cannot be a blind divider:
 * every sender above blocks on `while(MARS_SYS_COMM0)`, and HUD text is one
 * command per tile, so slowing the service rate outright would just move the
 * cost onto the primary. The 68K side backs off only after consecutive EMPTY
 * polls and snaps back to every-iteration on the first live command. */
void HwMdSetBusThrottle(int on) {
	while(MARS_SYS_COMM0) ;
	MARS_SYS_COMM0 = (unsigned short)(0x1300 | (on ? 1 : 0));
	while(MARS_SYS_COMM0) ;
}

/* FULLSCREEN-ON-32X: switch the modal mini-game's picture from MD plane-B tiles
 * to a raw tile-id broadcast this side renders itself. See md_main.c cmd 21.
 * mode: bit0 = broadcast on, bit1 = dirty-epoch protocol (TESTING>EPOCH). */
void HwMdSetSmsTileBcast(int mode) {
	while(MARS_SYS_COMM0) ;
	MARS_SYS_COMM0 = (unsigned short)(0x1500 | (mode & 3));
	while(MARS_SYS_COMM0) ;
}

void HwMdSmsBoot(void) {
	while(MARS_SYS_COMM0) ;
	MARS_SYS_COMM0 = 0x0900; // upload Z80 hello, drop VDP to mode 4, run
	while(MARS_SYS_COMM0) ;
}

void HwMdSmsStop(void) {
	/* BOUNDED (see the exit-hang saga). The 68K sweeps the splash rows and
	 * parks the Z80 — nothing more. The old "full restore" tail (register
	 * replay + CRAM repaint + $3800 sweep) was the grey-menu font-eraser:
	 * two bisect rounds proved any exit that skipped it kept the font, and
	 * in the mode-5-only design it restores state that was never touched. */
	uint32_t guard = 2000000;
	while (MARS_SYS_COMM0 && --guard) ;
	MARS_SYS_COMM0 = 0x0A00;
	guard = 2000000;
	while (MARS_SYS_COMM0 && --guard) ;
}

void HwMdSmsGameMap(const unsigned char *packed) {
	/* Stream the 440-byte level patch (1bpp world_map + spawn + exit +
	 * the 16-tile level name + the two edge-partition bitmaps) as 220
	 * indexed words. Each word is stateless (index rides in the command
	 * low byte — 220 still fits its 8 bits), so nothing can shear. */
	for (int i = 0; i < 220; i++) {
		while (MARS_SYS_COMM0) ;
		MARS_SYS_COMM2 = (unsigned short)((packed[i * 2] << 8) | packed[i * 2 + 1]);
		MARS_SYS_COMM0 = (unsigned short)(0x0B00 | i);
	}
	while (MARS_SYS_COMM0) ;
}

void HwMdSmsGameBoot(void) {
	while (MARS_SYS_COMM0) ;
	MARS_SYS_COMM0 = 0x0C00; // upload mini-game, patch map in, run the Z80
	while (MARS_SYS_COMM0) ;
}

void HwMdSmsGameStop(void) {
	/* BOUNDED like HwMdSmsStop — no COMM handshake may hold the exit. */
	uint32_t guard = 2000000;
	while (MARS_SYS_COMM0 && --guard) ;
	MARS_SYS_COMM0 = 0x0D00;
	guard = 2000000;
	while (MARS_SYS_COMM0 && --guard) ;
}

void HwMdYmCmd(unsigned char op) {
	/* Whole-action YM control (command 0x0F): 0 all off, 1 patch upload
	 * + bed on, 2 sting key-on, 3 sting release. One COMM round trip —
	 * the per-register path costs a frame per write (68K serves one
	 * command per vblank) and a 70-write upload visibly hung the game. */
	uint32_t guard = 2000000;
	while (MARS_SYS_COMM0 && --guard) ;
	MARS_SYS_COMM0 = (unsigned short)(0x0F00 | op);
	guard = 2000000;
	while (MARS_SYS_COMM0 && --guard) ;
}

void HwMdYmWrite(unsigned char reg, unsigned char val) {
	/* One YM2612 part-I register write, relayed to the 68K (command
	 * 0x0E). FIRE-AND-FORGET: waits for the slot to be free, posts, and
	 * returns — no completion wait. The 68K serves ~one command per
	 * vblank, so a caller pacing itself to one write per frame never
	 * blocks at all, while a synchronous burst is both a hang (16ms per
	 * write) and, empirically, silent (the B00246 mystery: 70-write
	 * bursts never took effect on the chip even with settle pacing —
	 * the frame-spaced stream is the PROVEN-sounding shape). */
	uint32_t guard = 2000000;
	while (MARS_SYS_COMM0 && --guard) ;
	MARS_SYS_COMM2 = val;
	MARS_SYS_COMM0 = (unsigned short)(0x0E00 | reg);
}

void HwMdSmsGlassBoot(void) {
	/* Headless mini-game boot for the PVM glass: same upload + level
	 * patch as the modal boot, but no text-layer blit and pad held 0 —
	 * the picture leaves over the COMM6/COMM10 broadcast instead. */
	while (MARS_SYS_COMM0) ;
	MARS_SYS_COMM0 = 0x1000;
	while (MARS_SYS_COMM0) ;
}

void HwMdSmsGlassHandoff(void) {
	/* Glass -> fullscreen with the SAME Z80 still running: the 68K just
	 * starts blitting and feeding the pad. No reboot means the chime and
	 * the music never restart across the cut. */
	uint32_t guard = 2000000;
	while (MARS_SYS_COMM0 && --guard) ;
	MARS_SYS_COMM0 = 0x1200;
	guard = 2000000;
	while (MARS_SYS_COMM0 && --guard) ;
}

void HwMdSmsGlassStop(void) {
	/* BOUNDED like every SMS teardown. */
	uint32_t guard = 2000000;
	while (MARS_SYS_COMM0 && --guard) ;
	MARS_SYS_COMM0 = 0x1100;
	guard = 2000000;
	while (MARS_SYS_COMM0 && --guard) ;
}

void HwMdSetColor(unsigned short index, unsigned short color) {
	while(MARS_SYS_COMM0) ; // wait until 68000 has responded to any earlier requests
	MARS_SYS_COMM2 = color;
	MARS_SYS_COMM0 = 0x0800 | (index & 0xFF); // Set MD CRAM entry (BGR word)
	while(MARS_SYS_COMM0) ;
}

static void NextChr(char c, uint16_t color) {
	if(c >= '0' && c <= '9') {
		c = c - '0' + 2;
	} else if(c >= 'A' && c <= 'Z') {
		c = c - 'A' + 12;
	} else if(c >= 'a' && c <= 'z') {
		c = c - 'a' + 12;
	} else if(c == ':') { c = 38;
	} else if(c == '.') { c = 39;
	} else if(c == '-') { c = 40;
	} else if(c == '>') { c = 41;
	} else if(c == '|') { c = 42;
	} else if(c == '+') { c = 43;
	} else if(c == '%') { c = 44;
	} else if(c == ' ') {
		c = 0;
	} else {
		c = 1;
	}
	HwMdSetNTable(c | color);
}

void HwMdPuts(char *str, uint16_t color, int x, int y) {
	HwMdSetOffset(((y<<6) | x) << 1);
	while(*str) NextChr(*str++, color);
}

void HwMdPutc(char chr, uint16_t color, int x, int y) {
	HwMdSetOffset(((y<<6) | x) << 1);
	NextChr(chr, color);
}
