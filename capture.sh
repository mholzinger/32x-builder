#!/bin/bash
# Extract frames from a screen recording and remove consecutive duplicates.
# Usage:
#   ./capture.sh                      # full recording, default 2:50
#   ./capture.sh 00:01:30             # first 1m30s only
#   ./capture.sh 00:00:10 00:01:00    # from 10s to 1m
#   ./capture.sh dedup                # just dedup existing frames (skip extract)
#   ./capture.sh raw [start] [end]    # NO dedup, keep frame_NNNNNN.png — for TIMING
#
# WHICH MODE?
#   default (dedup) -> VISUAL debugging. Hunting a seam, a wrong tile, a ghost
#                      row. Duplicate frames are noise; killing them makes the
#                      interesting frames easy to page through.
#   raw             -> TIMING / tools/frame_profiler.py. That tool reads how
#                      many consecutive capture frames are identical: each 32X
#                      frame is held across N display refreshes, so run length
#                      IS the frame time. Dedup deletes the repeats, which is
#                      the entire signal -- deduped input measures nothing.
#                      raw keeps every recorded frame, so the number means what
#                      it says.

set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
SSDIR="$DIR/screenshots"
mkdir -p "$SSDIR"

dedup_only=false
raw=false
start="00:00:00"
end="00:02:50"

if [[ "${1:-}" == "dedup" ]]; then
    dedup_only=true
elif [[ "${1:-}" == "raw" ]]; then
    raw=true
    shift
    [[ $# -ge 1 ]] && end="$1"
    [[ $# -ge 2 ]] && { start="$1"; end="$2"; }
elif [[ $# -eq 1 ]]; then
    end="$1"
elif [[ $# -eq 2 ]]; then
    start="$1"
    end="$2"
fi

# --- Extract ---
if [[ "$dedup_only" == false ]]; then
    rm -f "$SSDIR"/frame_*.png
    # Find newest Screen Recording on Desktop
    newest=$(find "$HOME/Desktop" -maxdepth 1 -name "Screen Recording*.mov" -print0 \
        | xargs -0 ls -t 2>/dev/null | head -1)
    if [[ -z "$newest" ]]; then
        echo "No Screen Recording found on Desktop"
        exit 1
    fi
    mv "$newest" "$SSDIR/capture.mov"
    # Dedup prefilter. The RMSE loop below is one `magick compare` process per
    # frame pair, each re-decoding both PNGs, and it is the whole runtime of
    # this script. mpdecimate drops near-duplicates inside ffmpeg's decode pass,
    # so the loop only ever sees survivors and the duplicates never hit disk.
    # A frame is dropped when no 8x8 block exceeds hi AND fewer than frac of the
    # blocks exceed lo, so LOWER hi/lo/frac == fewer drops.
    #
    # THIS CHANGES THE OUTPUT, it is not just a speedup. Measured on a 382-frame
    # recording: prefilter off -> 84 unique, prefilter on -> 193 unique. The
    # RMSE loop chains against the last KEPT frame, so a slow drift stays
    # anchored to one `prev` and the entire run gets deleted; thinning the run
    # first means each survivor clears 0.005 against the previous survivor and
    # is kept. Keeping MORE frames is the point -- the anchored chain silently
    # eats fades (CRT bloom, degauss, lighting ramps), which is exactly the
    # material this script exists to page through.
    # CAPTURE_PREFILTER=0 restores the old anchored-chain behaviour.
    # Array, not a string: the `64*4` in the filter is a glob to any shell that
    # gets a chance to look at it unquoted.
    prefilter=()
    if [[ "$raw" == false && "${CAPTURE_PREFILTER:-1}" != "0" ]]; then
        prefilter=(-vf "mpdecimate=hi=64*4:lo=64*2:frac=0.1")
    fi
    echo "Extracting frames ($start → $end)..."
    # -fps_mode passthrough: emit exactly the frames stored in the .mov, no
    # duplication or dropping to hit a constant rate. macOS screen recordings are
    # variable-frame-rate, and for timing work an ffmpeg-invented duplicate frame
    # would read as a real emulator frame.
    # NOTE: raw mode gets NO prefilter — see the TIMING note in the header.
    ffmpeg -hide_banner -loglevel error -ss "$start" -to "$end" \
        -i "$SSDIR/capture.mov" -fps_mode passthrough ${prefilter[@]+"${prefilter[@]}"} \
        "$SSDIR/frame_%06d.png" 2>/dev/null \
    || ffmpeg -hide_banner -loglevel error -ss "$start" -to "$end" \
        -i "$SSDIR/capture.mov" -vsync 0 ${prefilter[@]+"${prefilter[@]}"} \
        "$SSDIR/frame_%06d.png"
fi

if [[ "$raw" == true ]]; then
    n=$(ls "$SSDIR"/frame_*.png 2>/dev/null | wc -l | tr -d ' ')
    echo "raw mode: $n frames kept (no dedup, no renumber)."
    echo "  python3 tools/frame_profiler.py screenshots/"
    exit 0
fi

# --- Dedup visually identical consecutive frames ---
# Video compression means byte-identical is rare; use ImageMagick RMSE instead.
# Threshold 0.005 catches compression artifacts but keeps real scene changes.
THRESHOLD="0.005"

frames=("$SSDIR"/frame_*.png)
total=${#frames[@]}
if [[ "$total" -eq 0 ]]; then
    echo "No frames to dedup"
    exit 0
fi

echo "Deduplicating $total frames (RMSE threshold $THRESHOLD)..."
prev="${frames[0]}"
removed=0

set +e  # magick compare exits 1 when images differ
for ((i=1; i<total; i++)); do
    f="${frames[$i]}"
    # NOT `raw` — that name is the mode flag above, and clobbering it here is a
    # trap waiting for the next person who moves this loop.
    cmp_out=$(magick compare -metric RMSE "$prev" "$f" null: 2>&1)
    rmse=$(echo "$cmp_out" | grep -oE '\([0-9.e+-]+\)' | tr -d '()')
    if [[ -n "$rmse" ]] && (( $(echo "$rmse < $THRESHOLD" | bc -l) )); then
        rm "$f"
        ((removed++))
    else
        prev="$f"
    fi
done
set -e

remaining=$((total - removed))
echo "Done: $total extracted → $remaining unique ($removed duplicates removed)"

# --- Renumber surviving frames 1.png, 2.png, ... ---
echo "Renumbering frames..."
i=1
for f in "$SSDIR"/frame_*.png; do
    [[ -e "$f" ]] || continue
    mv "$f" "$SSDIR/$i.png"
    ((i++))
done
