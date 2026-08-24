#!/usr/bin/env python3
"""Measure delivered frame rate and stutter from a screen recording.

    python3 tools/frame_profiler.py screenshots/          # raw-mode PNGs
    python3 tools/frame_profiler.py screenshots/capture.mov   # straight from the .mov (faster)

WHAT IT MEASURES

A screen recording samples the display at the capture rate (~60fps). The 32X
delivers a new image every N vblanks, so each emulator frame appears in the
recording as a RUN of identical capture frames. Run length is the frame's
lifetime: held for 4 capture frames at 60fps capture == the game ran at 15fps
for that frame.

That makes this a direct, external measurement of what the player actually saw.
It needs nothing from the ROM -- no profiler build, no HUD counters, no
g_metrics_on -- so it does not pay the in-ROM profiler's ~2,255-tick overhead
and does not perturb what it measures.

STUTTER is not low average fps. It is VARIANCE: a run of 15fps frames with an
occasional 4-frame hitch reads worse than a steady 12fps. This reports the
distribution and names the worst offenders with timestamps, so you can go look
at the frame.

REQUIRES ./capture.sh raw
Default (dedup) mode DELETES the repeated frames, which is exactly the signal
this tool reads. Deduped input measures nothing.
"""

import argparse
import os
import re
import subprocess
import sys
from collections import Counter
from concurrent.futures import ThreadPoolExecutor

import numpy as np

# Comparison is done on a downscaled grayscale thumbnail: video compression
# means consecutive captures of a STATIC screen are never byte-identical, and
# downscaling averages away exactly that high-frequency noise.
THUMB = 256
# Normalized RMSE (0..1) on the THUMBNAIL, which is NOT the same number as
# capture.sh's full-res 0.005 -- downscaling shrinks RMSE by roughly 3x, so
# reusing 0.005 here silently merges frames capture.sh would keep.
# CALIBRATED 2026-08-13 against 381 full-res adjacent-pair comparisons: at
# 256px/0.0015 this makes the same keep-or-merge call as full-res/0.005 on
# 99.0% of pairs (2 missed, 2 extra), correlation 0.998. At 64px it was 97.4%,
# which is why the thumbnail is not smaller.
DEFAULT_THRESHOLD = 0.0015


def probe_fps(path):
    """Capture rate of the recording, as a float. None if unavailable."""
    try:
        out = subprocess.run(
            ["ffprobe", "-v", "error", "-select_streams", "v:0",
             "-show_entries", "stream=avg_frame_rate", "-of", "csv=p=0", path],
            capture_output=True, text=True, check=True).stdout.strip()
        num, _, den = out.partition("/")
        fps = float(num) / float(den or 1)
        return fps if fps > 0 else None
    except Exception:
        return None


def thumbs_from_mov(path):
    """Stream the whole recording through ffmpeg as tiny gray frames."""
    cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-i", path,
           "-fps_mode", "passthrough",
           "-vf", f"scale={THUMB}:{THUMB},format=gray",
           "-f", "rawvideo", "-"]
    raw = subprocess.run(cmd, capture_output=True, check=True).stdout
    n = len(raw) // (THUMB * THUMB)
    if n == 0:
        sys.exit(f"no frames decoded from {path}")
    return np.frombuffer(raw, np.uint8)[:n * THUMB * THUMB].reshape(n, THUMB, THUMB), None


def thumbs_from_dir(path):
    """Decode raw-mode PNGs in parallel. Returns (array, filenames)."""
    from PIL import Image

    names = sorted(f for f in os.listdir(path)
                   if re.fullmatch(r"frame_\d+\.png", f))
    if not names:
        # Deduped+renumbered output looks like 1.png, 2.png, ...
        if any(re.fullmatch(r"\d+\.png", f) for f in os.listdir(path)):
            sys.exit(f"{path} holds DEDUPED frames (1.png, 2.png ...).\n"
                     "Hold length is the signal here and dedup deleted it.\n"
                     "Re-capture with: ./capture.sh raw")
        sys.exit(f"no frame_NNNNNN.png in {path} -- run ./capture.sh raw")

    def load(name):
        with Image.open(os.path.join(path, name)) as im:
            return np.asarray(im.convert("L").resize((THUMB, THUMB)), np.uint8)

    # PIL releases the GIL inside decode, so threads actually parallelize here.
    with ThreadPoolExecutor(max_workers=min(16, (os.cpu_count() or 4))) as ex:
        arr = np.stack(list(ex.map(load, names)))
    return arr, names


def runs_of_held_frames(thumbs, threshold):
    """Group consecutive near-identical captures. Returns [(start_idx, length)].

    Compares each capture to its IMMEDIATE predecessor, not to the last
    distinct frame: the question is "did the display change between these two
    refreshes", which is a per-adjacent-pair question. (capture.sh's dedup
    chains against the last KEPT frame instead -- correct for its job, wrong
    for this one, since a slow fade would collapse into a single fake-long
    hold.)
    """
    a = thumbs.astype(np.float32)
    # Vectorized normalized RMSE for every adjacent pair at once.
    diff = a[1:] - a[:-1]
    rmse = np.sqrt((diff * diff).reshape(len(diff), -1).mean(axis=1)) / 255.0
    changed = rmse >= threshold

    runs, start = [], 0
    for i, c in enumerate(changed):
        if c:
            runs.append((start, i + 1 - start))
            start = i + 1
    runs.append((start, len(thumbs) - start))
    return runs, rmse


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("target", nargs="?", default="screenshots",
                    help="raw-mode frame directory, or a .mov (default: screenshots)")
    ap.add_argument("--threshold", type=float, default=DEFAULT_THRESHOLD,
                    help=f"normalized RMSE for 'the screen changed' (default {DEFAULT_THRESHOLD})")
    ap.add_argument("--capture-fps", type=float, default=None,
                    help="override the recording's own rate")
    ap.add_argument("--worst", type=int, default=10, help="how many hitches to list")
    args = ap.parse_args()

    if not os.path.exists(args.target):
        sys.exit(f"no such path: {args.target}")

    if os.path.isdir(args.target):
        thumbs, names = thumbs_from_dir(args.target)
        mov = os.path.join(args.target, "capture.mov")
        cap_fps = args.capture_fps or (probe_fps(mov) if os.path.exists(mov) else None)
        source = f"{len(thumbs)} PNGs in {args.target}"
    else:
        thumbs, names = thumbs_from_mov(args.target)
        cap_fps = args.capture_fps or probe_fps(args.target)
        source = f"{len(thumbs)} frames in {args.target}"

    if len(thumbs) < 3:
        sys.exit("need at least 3 frames")

    fps_known = cap_fps is not None
    if not fps_known:
        cap_fps = 60.0

    runs, _ = runs_of_held_frames(thumbs, args.threshold)
    # Drop the final run: the recording ends mid-frame, so its length is an
    # artifact of when you stopped recording, not of engine cost.
    if len(runs) > 1:
        runs = runs[:-1]
    holds = np.array([r[1] for r in runs])

    print(f"Source: {source}")
    print(f"Capture rate: {cap_fps:.2f} fps" + ("" if fps_known else "  (ASSUMED -- no .mov to probe)"))
    print(f"Distinct game frames: {len(runs)} of {len(thumbs)} captures\n")

    mean_fps = cap_fps / holds.mean()
    print(f"Delivered fps  mean {mean_fps:5.2f}   "
          f"median {cap_fps / np.median(holds):5.2f}   "
          f"worst {cap_fps / holds.max():5.2f}")
    print(f"Frame time     mean {1000 * holds.mean() / cap_fps:6.1f} ms   "
          f"worst {1000 * holds.max() / cap_fps:6.1f} ms\n")

    print("Hold  fps     frames   share")
    total = len(holds)
    for hold, count in sorted(Counter(holds.tolist()).items()):
        share = count / total
        bar = "#" * max(1, round(share * 40)) if share > 0.005 else ""
        print(f"{hold:4d}  {cap_fps / hold:5.1f}   {count:6d}   {share:5.1%} {bar}")

    # A hitch is a frame that lived at least twice as long as the local norm.
    med = float(np.median(holds))
    hitch = [(i, h) for i, h in enumerate(holds) if h >= max(2 * med, med + 2)]
    print(f"\nHitches (hold >= {max(2 * med, med + 2):.0f} captures): "
          f"{len(hitch)} of {total} frames ({len(hitch) / total:.1%})")

    if hitch:
        print(f"\nWorst {min(args.worst, len(hitch))}:")
        print("  t(s)     held   fps    frame")
        for i, h in sorted(hitch, key=lambda x: -x[1])[:args.worst]:
            start = runs[i][0]
            who = names[start] if names else f"capture #{start + 1}"
            print(f"  {start / cap_fps:7.2f}  {h:4d}  {cap_fps / h:5.1f}   {who}")

    # Steadiness matters more than the mean -- see the module docstring.
    print(f"\nJitter: hold stddev {holds.std():.2f} captures "
          f"({'steady' if holds.std() < 0.5 else 'uneven' if holds.std() < 1.5 else 'STUTTERY'})")


if __name__ == "__main__":
    main()
