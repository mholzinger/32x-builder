#!/usr/bin/env python3
"""Rebuild the exact measured per-page/per-mode ROM bytes from frozen base images."""
from pathlib import Path
import argparse,hashlib,json,zipfile
root=Path(__file__).resolve().parents[2];evidence=root/'devdoc/sh2zam-nightly-2026-11-16'
p=argparse.ArgumentParser(description=__doc__);p.add_argument('profile',choices=['native','view','dual','game','game-ram']);p.add_argument('--output-dir',type=Path,required=True);a=p.parse_args()
base_name,manifest_name={'native':('native-final.32x','final-manifest.json'),'dual':('dual-matched.32x','dual-matched-manifest.json'),'view':('viewclip-initial.32x','viewclip-manifest.json'),'game':('game-mac-rom.32x','game-mac-manifest.json'),'game-ram':('game-mac-ram.32x','game-mac-ram-manifest.json')}[a.profile]
with zipfile.ZipFile(evidence/'measured-roms.zip') as z:raw=z.read(base_name)
off=raw.index(b'DIAGCFG1');outputs=[]
for row in json.loads((evidence/manifest_name).read_text()):
 data=bytearray(raw)
 if a.profile in ['native','view','dual']:
  if a.profile=='native' and row['page'] in [25,26]:continue
  if a.profile=='view' and row['page'] in [20,24]:continue # replaced by corrected native composite captures
  data[off+31]=row['page'];name=f'nightly-{a.profile}-{row["page"]}.32x'
 else:
  obj=next(v for v in ['deskset','chair','desk','pvm'] if v in row['file']);scene={'chair':6,'desk':7,'pvm':8,'deskset':9}[obj]
  data[off+8:off+32]=bytes(24)
  for key,value in {9:1,10:1,13:1,18:1,26:38,27:scene,28:1,29:row['mode'],30:1}.items():data[off+key]=value
  name=Path(row['file']).name
 if hashlib.sha256(data).hexdigest()!=row['sha256']:p.error(f'manifest mismatch: {row}')
 path=a.output_dir/name
 if path.exists():p.error(f'refusing overwrite: {path}')
 outputs.append((path,data))
a.output_dir.mkdir(parents=True,exist_ok=True)
for path,data in outputs:path.write_bytes(data)
print(f'{len(outputs)} exact measured ROMs reproduced and SHA256-verified')
