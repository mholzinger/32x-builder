#!/bin/sh
for object in chair desk pvm; do
  for arm in 0 2 1 0; do
    if [ "$arm" = 0 ]; then
      if [ "$previous" = 1 ]; then pass=b; else pass=a; fi
    else pass=a; fi
    printf 'load_core /media/fat/fdiv-%s-%s.mgl\n' "$object" "$arm" > /dev/MiSTer_cmd
    sleep 22
    printf 'screenshot fdiv-%s-%s-%s\n' "$object" "$arm" "$pass" > /dev/MiSTer_cmd
    sleep 2
    echo "captured $object mode=$arm pass=$pass"
    previous=$arm
  done
done
printf 'load_core /media/fat/backrooms-balanced.mgl\n' > /dev/MiSTer_cmd
