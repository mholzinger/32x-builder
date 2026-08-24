#!/usr/bin/env python3
"""Generative liminal music candidates for the SMS mini-game (DESIGN.md 4a).

One engine, four variants, two outputs each:
  sound/sms_liminal/<variant>.mid   audition the composition through Fusion
  sound/sms_liminal/<variant>.wav   what the SN76489 will actually do to it

The engine is written Z80-honest so the winner ports mechanically:
- state advances once per 60 Hz frame, integer math only
- 16-bit Galois LFSR for every random choice
- pitches exist ONLY as 10-bit PSG tone dividers from small tables
- volumes are 4-bit PSG attenuations stepped at frame granularity

PSG model: tone freq = 3579545 / (32 * divider); attenuation 2 dB/step,
15 = silent. Detune is done the hardware way, divider + 1, which beats at
a few Hz in the drone register.

MIDI mapping: drone A ch1, drone B ch2 (pitch-bent to the exact detuned
divider frequency), melody ch3. No program changes — assign FM patches in
Fusion. 60 bpm, 480 tpq, so one beat = one second = 60 frames.
"""
import math
import pathlib
import struct

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "sound" / "sms_liminal"
OUT.mkdir(parents=True, exist_ok=True)

PSG_CLOCK = 3579545
SR = 44100
SECONDS = 90
FRAMES = SECONDS * 60


def div_freq(n):
    return PSG_CLOCK / (32.0 * n)


def div_for(freq):
    return max(1, min(1023, round(PSG_CLOCK / (32.0 * freq))))


def midi_note_and_bend(freq):
    """Nearest MIDI note + 14-bit pitch bend (+/-2 semitone range)."""
    m = 69 + 12 * math.log2(freq / 440.0)
    note = round(m)
    bend = int(8192 + (m - note) * 4096)
    return note, max(0, min(16383, bend))


class LFSR:
    def __init__(self, seed):
        self.s = seed & 0xFFFF or 1

    def step(self):
        lsb = self.s & 1
        self.s >>= 1
        if lsb:
            self.s ^= 0xB400
        return self.s

    def rand(self, n):
        return self.step() % n


# Divider tables (built from note names for readability; the Z80 port
# stores the resulting dividers only).
def dv(name):
    names = {"C": 0, "D": 2, "E": 4, "F": 5, "F#": 6, "G": 7, "A": 9,
             "A#": 10, "B": 11}
    letter = name[:-1]
    octave = int(name[-1])
    semis = (octave + 1) * 12 + names[letter]
    return div_for(440.0 * 2 ** ((semis - 69) / 12.0))


VARIANTS = {
    # whole-tone drones, near-static, very sparse melody high above
    "A-wholetone": dict(
        drones=[dv(n) for n in ("C2", "D2", "E2", "F#2")],
        melody=[dv(n) for n in ("C4", "D4", "E4", "F#4", "A#4")],
        drone_hold=(12, 30), melody_gap=(4, 14), melody_fade=90,
        detune=1, noise=False),
    # lydian fragments, slightly denser, brighter register
    "B-lydian": dict(
        drones=[dv(n) for n in ("C2", "G2", "F#2", "E2")],
        melody=[dv(n) for n in ("E4", "F#4", "G4", "B4", "D5")],
        drone_hold=(10, 22), melody_gap=(3, 9), melody_fade=60,
        detune=1, noise=False),
    # variant A plus a periodic-noise air-handler swell every so often
    "C-airhandler": dict(
        drones=[dv(n) for n in ("C2", "D2", "E2", "F#2")],
        melody=[dv(n) for n in ("C4", "D4", "E4", "F#4", "A#4")],
        drone_hold=(12, 30), melody_gap=(5, 15), melody_fade=90,
        detune=1, noise=True),
    # bare fourths, low, the emptiest of the four
    "D-fourths": dict(
        drones=[dv(n) for n in ("C2", "F2", "G2", "C2")],
        melody=[dv(n) for n in ("C4", "F4", "G4", "C5")],
        drone_hold=(16, 40), melody_gap=(6, 18), melody_fade=120,
        detune=2, noise=True),
}


def run_engine(cfg, seed):
    """Frame-stepped engine -> event log: (frame, ch, divider|None, atten).

    ch: 0 drone A, 1 drone B (detuned), 2 melody, 3 noise.
    """
    rng = LFSR(seed)
    ev = []
    drone_i = 0
    drone_t = 0
    mel_wait = 60
    mel_att = 15
    mel_div = cfg["melody"][0]
    mel_fade_ctr = 0
    noise_att = 15
    noise_t = 0

    for f in range(FRAMES):
        # drones: pick a new table entry when the hold expires; 6 dB in
        if f == 0 or drone_t == 0:
            if f == 0 or rng.rand(4) != 0:
                drone_i = rng.rand(len(cfg["drones"]))
            d = cfg["drones"][drone_i]
            ev.append((f, 0, d, 3))
            ev.append((f, 1, d + cfg["detune"], 4))
            lo, hi = cfg["drone_hold"]
            drone_t = (lo + rng.rand(hi - lo)) * 60
        drone_t -= 1

        # melody: rare onset, long stepped fade
        if mel_wait == 0 and mel_att == 15:
            mel_div = cfg["melody"][rng.rand(len(cfg["melody"]))]
            mel_att = 4
            mel_fade_ctr = cfg["melody_fade"]
            ev.append((f, 2, mel_div, mel_att))
        elif mel_att < 15:
            mel_fade_ctr -= 1
            if mel_fade_ctr <= 0:
                mel_att += 1
                mel_fade_ctr = cfg["melody_fade"]
                ev.append((f, 2, None, mel_att))
                if mel_att == 15:
                    lo, hi = cfg["melody_gap"]
                    mel_wait = (lo + rng.rand(hi - lo)) * 60
        else:
            mel_wait -= 1

        # noise: occasional swell, attack and release both stepped
        if cfg["noise"]:
            if noise_att == 15 and noise_t == 0 and rng.rand(600) == 0:
                noise_att = 12
                noise_t = 120
                ev.append((f, 3, None, noise_att))
            elif noise_att < 15:
                noise_t -= 1
                if noise_t <= 0:
                    noise_att += 1
                    noise_t = 40
                    ev.append((f, 3, None, noise_att))
    return ev


# ---------------- WAV: piecewise-constant SN76489 render ----------------
def render_wav(ev, path, seconds=SECONDS):
    n = seconds * SR
    frames_end = seconds * 60
    t_all = np.arange(n) / SR
    audio = np.zeros(n)
    # per channel: build (start_frame, divider, atten) segments
    for ch in range(4):
        segs = []
        cur_div, cur_att = None, 15
        for f, c, d, a in ev:
            if c != ch:
                continue
            if d is not None:
                cur_div = d
            cur_att = a
            segs.append((f, cur_div, cur_att))
        for i, (f0, d, a) in enumerate(segs):
            if d is None and ch != 3:
                continue
            f1 = segs[i + 1][0] if i + 1 < len(segs) else frames_end
            s0, s1 = f0 * SR // 60, min(n, f1 * SR // 60)
            if s1 <= s0 or a >= 15:
                continue
            amp = 0.22 * (10 ** (-a * 2 / 20.0))
            t = t_all[s0:s1]
            if ch == 3:
                # d carries the noise ctrl byte when the event set one
                # (drums); None = the legacy air-handler swell.
                if d is not None and (d & 4) == 0:
                    # periodic noise: a 1-in-15 pulse train at rate/15
                    rate = PSG_CLOCK / (512 << (d & 3))
                    phase = (t * rate / 15.0) % 1.0
                    seg = np.where(phase < (1 / 15.0), 1.0, -1.0)
                else:
                    # white noise, duller at the slower shift rates
                    width = 6 << (d & 3) if d is not None else 24
                    seg = np.random.default_rng(f0).uniform(-1, 1, s1 - s0)
                    seg = np.convolve(seg, np.ones(width) / width,
                                      mode="same")
            else:
                phase = (t * div_freq(d)) % 1.0
                seg = np.where(phase < 0.5, 1.0, -1.0)
            audio[s0:s1] += amp * seg
    # gentle low-pass to tame square edges, then normalize headroom
    audio = np.convolve(audio, np.ones(3) / 3, mode="same")
    peak = np.max(np.abs(audio)) or 1.0
    pcm = (audio / peak * 0.8 * 32767).astype("<i2")
    with open(path, "wb") as fh:
        fh.write(b"RIFF" + struct.pack("<I", 36 + pcm.nbytes) + b"WAVE")
        fh.write(b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, SR, SR * 2, 2, 16))
        fh.write(b"data" + struct.pack("<I", pcm.nbytes))
        fh.write(pcm.tobytes())


# ---------------- MIDI: same events as notes ----------------
def vlq(v):
    out = [v & 0x7F]
    while v > 0x7F:
        v >>= 7
        out.append((v & 0x7F) | 0x80)
    return bytes(reversed(out))


def render_midi(ev, path):
    TPQ = 480          # 60 bpm -> 1 beat = 1 s = 60 frames
    def ticks(frame):
        return frame * TPQ // 60

    # per MIDI channel: (frame, kind, data)
    tracks = []
    for ch in range(3):   # noise left out of the MIDI audition
        msgs = []
        cur_note = None
        segs = [(f, d, a) for f, c, d, a in ev if c == ch]
        for i, (f, d, a) in enumerate(segs):
            if d is not None and a < 15:
                freq = div_freq(d)
                note, bend = midi_note_and_bend(freq)
                if cur_note is not None:
                    msgs.append((f, 0x80 | ch, cur_note, 0))
                msgs.append((f, 0xE0 | ch, bend & 0x7F, bend >> 7))
                vel = int((15 - a) / 15 * 90) + 20
                msgs.append((f, 0x90 | ch, note, vel))
                cur_note = note
            elif a >= 15 and cur_note is not None:
                msgs.append((f, 0x80 | ch, cur_note, 0))
                cur_note = None
            elif a < 15 and cur_note is not None and d is None:
                # fade step -> channel volume CC
                msgs.append((f, 0xB0 | ch, 7, int((15 - a) / 15 * 127)))
        if cur_note is not None:
            msgs.append((FRAMES, 0x80 | ch, cur_note, 0))
        data = b""
        last = 0
        for f, st, d1, d2 in msgs:
            data += vlq(ticks(f) - ticks(last)) + bytes([st, d1 & 0x7F, d2 & 0x7F])
            last = f
        data += b"\x00\xff\x2f\x00"
        tracks.append(data)

    tempo = b"\x00\xff\x51\x03" + (1000000).to_bytes(3, "big")  # 60 bpm
    tracks.insert(0, tempo + b"\x00\xff\x2f\x00")
    with open(path, "wb") as fh:
        fh.write(b"MThd" + struct.pack(">IHHH", 6, 1, len(tracks), TPQ))
        for tr in tracks:
            fh.write(b"MTrk" + struct.pack(">I", len(tr)) + tr)


# ---------------- B2: the exact Z80 port of the chosen B-lydian ----------
# What the maze game will actually play. Static drone pair (the PSG bottoms
# out at divider 1023, which is where all of B's low drones landed anyway):
# ch0 div 1022 att 3, ch1 div 1023 att 4, beating at ~0.107 Hz. Melody ch2
# from an 8-entry divider table indexed by lfsr&7, onset gap from an 8-entry
# frame table, attenuation 4 stepped to 15 once per 60 frames. Seed =
# 0xACE1 ^ (spawn_y<<8|spawn_x) ^ (exit_y<<8|exit_x). The Z80 code in
# games/maze/game.asm mirrors this function line for line; the simulator
# asserts byte-for-byte parity of the PSG write stream.
MEL_DIVS = [339, 302, 285, 226, 190, 339, 285, 226]   # E4 F#4 G4 B4 D5 E4 G4 B4
GAP_TAB = [180, 240, 300, 360, 420, 480, 540, 600]    # frames


def run_engine_port(seed, frames=FRAMES, drones=(1022, 1023),
                    mel=MEL_DIVS, gaps=GAP_TAB):
    rng = LFSR(seed)
    ev = [(0, 0, drones[0], 3), (0, 1, drones[1], 4)]
    att, wait, fade, div = 15, 120, 0, mel[0]
    for f in range(frames):
        if att == 15:
            if wait == 0:
                div = mel[rng.step() & 7]
                att, fade = 4, 60
                ev.append((f, 2, div, att))
            else:
                wait -= 1
        else:
            fade -= 1
            if fade == 0:
                fade = 60
                att += 1
                ev.append((f, 2, None, att))
                if att == 15:
                    wait = gaps[rng.step() & 7]
    return ev


# ---- INN: derived from the At the Inn transcription (Shadowgate 64) -----
# sound/basic_pitch_cleaned.mid analysis: G minor, G 47% of all notes,
# bass G-D-A, top voice circling G4/Bb4/A4 with minor-third steps and
# octave leaps. G2 sits below the PSG's divider range, so INN-A drones on
# the fifth (D3 pair) and INN-B keeps the sub-A2 clamp pair as a rootless
# haze under the same G-minor melody.
INN_MEL = [285, 240, 254, 190, 285, 214, 240, 143]  # G4 Bb4 A4 D5 G4 C5 Bb4 G5
INN_DRONES_A = (762, 763)      # D3 pair, beats ~0.19 Hz
INN_DRONES_B = (1022, 1023)    # the eerie between-notes floor


def port_psg_bytes(seed, frames):
    """The exact PSG byte stream the Z80 emits (for the simulator)."""
    out = [0x9F, 0xBF, 0xDF, 0xFF,                 # boot silence
           0x8E, 0x3F, 0x93, 0xAF, 0x3F, 0xB4]     # drone pair
    for f, ch, div, att in run_engine_port(seed, frames)[2:]:
        if div is not None:
            out += [0xC0 | (div & 0xF), (div >> 4) & 0x3F]
        out.append(0xD0 | att)
    return out


for name, cfg in VARIANTS.items():
    events = run_engine(cfg, seed=0xACE1)
    render_wav(events, OUT / f"{name}.wav")
    render_midi(events, OUT / f"{name}.mid")
    print(f"{name}: {len(events)} PSG events -> {name}.wav / {name}.mid")

# ---- SPACE: phrase + echo engine (the "spacey, delightful, haunting" take)
# The drone take read as empty, not haunting. This one moves: ch2 plays
# short motifs from the Inn's actual melodic cells with music-box decay,
# ch1 is a pseudo-delay replaying every melody note 21 frames later and
# 10 dB quieter (THE chip-music space trick), ch0 walks a slow bass line
# (D3/C3/Bb2 — i-less Gm motion, the root lives in the melody). Still
# integer state + LFSR choices at 60 Hz, so the Z80 port stays mechanical.
SPACE_MOTIFS = [
    [285, 240, 254, 285],        # G4 Bb4 A4 G4   (the circling figure)
    [190, 214, 240, 285],        # D5 C5 Bb4 G4   (descending answer)
    [143, 190, 240],             # G5 D5 Bb4      (the octave-leap fall)
    [254, 240, 285, 190],        # A4 Bb4 G4 D5   (turn + lift)
]
SPACE_BASS = [762, 855, 960, 762]     # D3 C3 Bb2 D3
# A2 tempo (Mike: "a little faster"): notes 13-20 frames apart (was
# 18-25), phrase gaps 1.25-4.75 s (was 2-7.25), bass every 5.5-9 s (was
# 7-10.5), echo tightened to 18 frames so it stays behind the next note.
ECHO_DELAY = 18
ECHO_ATT = 5                          # added attenuation (~10 dB down)

# ---- drums: the Aliens march (Horner's quiet military snare) ----------
# LFSR-free fixed loops of (noise ctrl, attenuation, frames to next hit)
# so the melody/bass random streams are untouched by the drum layer.
# $E6 = white noise clk/2048 (the dull distant snare); $E2 = periodic
# noise clk/2048, a ~116 Hz thud. Hits decay att+1 every 2 frames.
DRUM_PATTERNS = {
    # two-bar march cadence, 45 f/beat (~80 bpm): accent, tap, drag ruff
    # into beat 3, tap; bar 2 adds a pickup ghost into the loop.
    # THE ROM PATTERN — mirrors drum_pat in game.asm byte for byte.
    "A-march": [
        (0xE6, 4, 45), (0xE6, 6, 39), (0xE6, 8, 3), (0xE6, 8, 3),
        (0xE6, 4, 45), (0xE6, 6, 45), (0xE6, 4, 45), (0xE6, 6, 39),
        (0xE6, 8, 3), (0xE6, 8, 3), (0xE6, 4, 45), (0xE6, 6, 18),
        (0xE6, 8, 27)],
    # 60 bpm heartbeat: periodic-noise thuds on 1 and 3, ghost taps
    # between — darker, less snare, more machine-room.
    "B-heartbeat": [
        (0xE2, 4, 60), (0xE6, 8, 60), (0xE2, 5, 60), (0xE6, 8, 54),
        (0xE6, 8, 6)],
    # sparse and cinematic: a quiet four-stroke ruff into an accent,
    # one lone answering tap, then five seconds of nothing.
    "C-distant": [
        (0xE6, 9, 3), (0xE6, 8, 3), (0xE6, 9, 3), (0xE6, 8, 3),
        (0xE6, 5, 180), (0xE6, 7, 258)],
}

# ---- melody instruments: envelope shapes for the SN76489 --------------
# The chip has no timbre — three identical squares — so an "instrument"
# is a volume envelope: a list of (frames to hold, attenuation) played
# from note-on, then silence. Table-driven, so the winner ports to the
# Z80 exactly like drum_pat did. "chorus" replaces the delayed echo
# with a simultaneous detuned (divider+1) double — the dreamy pad trick.
INSTRUMENTS = {
    # the current sound: bright attack, one step down every 6 frames
    "A-musicbox": dict(env=[(6, 2 + i) for i in range(13)]),
    # harder attack, fast linear decay — plucked string, drier
    "B-harp": dict(env=[(3, i) for i in range(15)]),
    # fast drop to a quiet level, then a long slow ring — struck bell
    "C-bell": dict(env=[(2, 3), (2, 4), (2, 5), (2, 6)]
                       + [(14, a) for a in range(7, 15)]),
    # reverse envelope: blooms in over 16 frames, holds, slow release —
    # bowed glass / pad; melts notes together at this note spacing
    "D-swell": dict(env=[(4, 11), (4, 9), (4, 7), (4, 5), (40, 4)]
                        + [(8, a) for a in range(5, 15)]),
    # music-box envelope but ch1 doubles every note at divider+1:
    # a slow ~4 Hz beat between the pair (trades the echo away)
    "E-chorus": dict(env=[(6, 2 + i) for i in range(13)], chorus=True),
}


def run_engine_space(seed, frames=FRAMES, vibrato=False, drums=None,
                     inst=None):
    rng = LFSR(seed)
    ev = []
    # melody phrase state
    motif, mi, note_t = [], 0, 90     # first phrase 1.5 s in
    m_att, m_fade = 15, 0
    m_div = 285
    gap = 0
    echo = []                          # (fire_frame, div) pending echoes
    e_att, e_fade, e_div = 15, 0, 0
    # bass state
    b_i, b_t, b_att, b_fade = 0, 0, 15, 0
    # drum state
    d_i, d_t, d_att, d_fade = 0, 0, 15, 0
    # instrument-envelope state (inst is not None)
    m_seq, m_hold = [], 0
    e_seq, e_hold = [], 0
    chorus = bool(inst and inst.get("chorus"))
    vib_phase = 0
    for f in range(frames):
        # ---- bass: one soft note per phrase-ish period, slow fade
        if b_t == 0:
            b_div = SPACE_BASS[b_i & 3]
            b_i += 1 if (rng.step() & 3) else 2   # mostly stepwise, odd skip
            b_att, b_fade = 6, 30
            ev.append((f, 0, b_div, b_att))
            b_t = 330 + (rng.step() & 7) * 30     # 5.5-9 s
        b_t -= 1
        if b_att < 15:
            b_fade -= 1
            if b_fade == 0:
                b_fade = 30
                b_att += 1
                ev.append((f, 0, None, b_att))
        # ---- melody: motif playback with music-box decay
        if mi >= len(motif):
            if gap == 0:
                motif = SPACE_MOTIFS[rng.step() & 3]
                mi = 0
                note_t = 0
                gap = 75 + (rng.step() & 7) * 30    # 1.25-4.75 s between phrases
            else:
                gap -= 1
        if mi < len(motif) and note_t == 0:
            m_div = motif[mi]
            mi += 1
            if inst is None:
                m_att, m_fade = 2, 6               # bright attack, quick decay
                ev.append((f, 2, m_div, m_att))
                echo.append((f + ECHO_DELAY, m_div))
            else:
                m_seq = list(inst["env"])
                m_hold, m_att = m_seq.pop(0)
                ev.append((f, 2, m_div, m_att))
                if chorus:
                    ev.append((f, 1, m_div + 1, min(15, m_att + 1)))
                else:
                    echo.append((f + ECHO_DELAY, m_div))
            note_t = 13 + (rng.step() & 7)         # lilting inter-note timing
        elif note_t > 0:
            note_t -= 1
        if inst is None:
            if m_att < 15:
                m_fade -= 1
                if m_fade == 0:
                    m_fade = 6
                    m_att += 1
                    ev.append((f, 2, None, m_att))
        elif m_hold > 0:
            m_hold -= 1
            if m_hold == 0:
                if m_seq:
                    m_hold, m_att = m_seq.pop(0)
                else:
                    m_att = 15
                ev.append((f, 2, None, m_att))
                if chorus:
                    ev.append((f, 1, None, min(15, m_att + 1)))
        if vibrato and m_att < 12:
            vib_phase = (vib_phase + 1) & 15
            if vib_phase == 0:
                ev.append((f, 2, m_div + 1, m_att))
            elif vib_phase == 8:
                ev.append((f, 2, m_div, m_att))
        # ---- echo channel: replay, quieter, same decay shape
        if echo and echo[0][0] == f:
            _, e_div = echo.pop(0)
            if inst is None:
                e_att, e_fade = 2 + ECHO_ATT, 6
            else:
                e_seq = [(h, min(15, a + ECHO_ATT)) for h, a in inst["env"]]
                e_hold, e_att = e_seq.pop(0)
            ev.append((f, 1, e_div, e_att))
        if inst is None:
            if e_att < 15:
                e_fade -= 1
                if e_fade == 0:
                    e_fade = 6
                    e_att += 1
                    ev.append((f, 1, None, e_att))
        elif not chorus and e_hold > 0:
            e_hold -= 1
            if e_hold == 0:
                if e_seq:
                    e_hold, e_att = e_seq.pop(0)
                else:
                    e_att = 15
                ev.append((f, 1, None, e_att))
        # ---- drums: fixed march loop on the noise channel (LFSR-free)
        if drums:
            if d_t == 0:
                ctrl, att, wait = drums[d_i]
                d_i = (d_i + 1) % len(drums)
                d_att, d_fade = att, 2
                ev.append((f, 3, ctrl, att))
                d_t = wait
            d_t -= 1
            if d_att < 15:
                d_fade -= 1
                if d_fade == 0:
                    d_fade = 2
                    d_att += 1
                    ev.append((f, 3, None, d_att))
    return ev


# ---- the boot chime: the SE-GA gesture reversed (sms_putrats) ----------
# Mirrors chime_tick in games/maze/game.asm: G-minor chord (G3/D4/G4)
# swells in from silence, holds, sweeps DOWN the 24-step table to the
# low cluster, cuts. Preview of exactly what boots in the mini-game.
def chime_events():
    CH = [(0, 571, 19, 1023), (1, 381, 16, 762), (2, 285, 12, 570)]
    ev = []
    att = 15
    for f in range(107):
        if f == 0:
            att -= 1
            for ch, tgt, _, _ in CH:
                ev.append((f, ch, tgt, att))
        elif f <= 36 and f % 3 == 0:
            att -= 1
            for ch, _, _, _ in CH:
                ev.append((f, ch, None, att))
        elif 82 <= f <= 105:
            i = 105 - f
            for ch, _, stp, start in CH:
                ev.append((f, ch, start - stp * i, att))
        elif f == 106:
            for ch, _, _, _ in CH:
                ev.append((f, ch, None, 15))
    return ev


render_wav(chime_events(), OUT / "CHIME-putrats.wav", seconds=3)
print("CHIME-putrats: the reversed boot chime preview")

port_events = run_engine_port(0xACE1 ^ 0x0202 ^ 0x101F)   # sim maze's seed
render_wav(port_events, OUT / "B2-lydian-port.wav")
render_midi(port_events, OUT / "B2-lydian-port.mid")
print(f"B2-lydian-port: {len(port_events)} PSG events (the Z80 version)")

for name, drones in (("INN-A-fifth-drone", INN_DRONES_A),
                     ("INN-B-floor-drone", INN_DRONES_B)):
    evs = run_engine_port(0xACE1 ^ 0x0202 ^ 0x101F, drones=drones, mel=INN_MEL)
    render_wav(evs, OUT / f"{name}.wav")
    render_midi(evs, OUT / f"{name}.mid")
    print(f"{name}: {len(evs)} PSG events (At the Inn derivation)")

for name, vib in (("SPACE-A-echo", False), ("SPACE-B-echo-vibrato", True)):
    evs = run_engine_space(0xACE1 ^ 0x0202 ^ 0x101F, vibrato=vib)
    render_wav(evs, OUT / f"{name}.wav")
    render_midi(evs, OUT / f"{name}.mid")
    print(f"{name}: {len(evs)} PSG events (phrase+echo engine)")

for dname, pat in DRUM_PATTERNS.items():
    evs = run_engine_space(0xACE1 ^ 0x0202 ^ 0x101F, drums=pat)
    render_wav(evs, OUT / f"DRUM-{dname}.wav")
    print(f"DRUM-{dname}: {len(evs)} PSG events (SPACE-A + drums)")

# instrument auditions: same notes, same march, only the melody
# envelope changes (A-musicbox is what the ROM plays today)
for iname, cfg in INSTRUMENTS.items():
    evs = run_engine_space(0xACE1 ^ 0x0202 ^ 0x101F,
                           drums=DRUM_PATTERNS["A-march"], inst=cfg)
    render_wav(evs, OUT / f"INST-{iname}.wav")
    print(f"INST-{iname}: {len(evs)} PSG events (envelope audition)")
print(f"output: {OUT}")
