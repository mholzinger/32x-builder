#!/bin/sh
for pass in a b; do
  for page in 1 2 3 4 5 6 7 8; do
    printf 'load_core /media/fat/sh2zam-math-%s.mgl\n' "$page" > /dev/MiSTer_cmd
    sleep 8
    printf 'screenshot sh2zam-contrib-%s-%s-final\n' "$page" "$pass" > /dev/MiSTer_cmd
    sleep 2
    echo "captured page=$page pass=$pass"
  done
done
printf "load_core /media/fat/backrooms-balanced.mgl\n" > /dev/MiSTer_cmd
