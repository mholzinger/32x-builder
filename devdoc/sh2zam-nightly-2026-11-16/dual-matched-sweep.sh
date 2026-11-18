#!/bin/sh
for pass in a b; do
 for page in 25 26; do
  printf 'load_core /media/fat/nightly-dual-matched-%s.mgl\n' "$page" > /dev/MiSTer_cmd
  sleep 10
  printf 'screenshot sh2zam-contrib-%s-%s-dual-matched\n' "$page" "$pass" > /dev/MiSTer_cmd
  sleep 2
  echo "captured matched dual page=$page pass=$pass"
 done
done
printf 'load_core /media/fat/backrooms-balanced.mgl\n' > /dev/MiSTer_cmd
