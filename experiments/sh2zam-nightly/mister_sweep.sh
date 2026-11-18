#!/bin/sh
# Run ON MiSTer after uploading nightly-page-*.32x and their MGLs.
set -eu
restore_mgl=${RESTORE_MGL:-/media/fat/backrooms-balanced.mgl}
restore() { printf 'load_core %s\n' "$restore_mgl" > /dev/MiSTer_cmd; }
trap restore EXIT
trap 'exit 130' HUP INT TERM
pages="$*"
if [ -z "$pages" ]; then pages='9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30'; fi
for pass in a b; do
 for page in $pages; do
  case "$page" in ''|*[!0-9]*) echo 'numeric page required' >&2; exit 1;; esac
  printf 'load_core /media/fat/nightly-page-%s.mgl\n' "$page" > /dev/MiSTer_cmd
  if [ "$page" = 30 ]; then sleep 45; else sleep 10; fi
  printf 'screenshot sh2zam-contrib-%s-%s-repeat\n' "$page" "$pass" > /dev/MiSTer_cmd
  sleep 2
  echo "captured page=$page pass=$pass"
 done
done
