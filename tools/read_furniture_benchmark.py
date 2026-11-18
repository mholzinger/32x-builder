#!/usr/bin/env python3
"""Decode native MiSTer furniture benchmark captions; reject invalid samples."""
import argparse,json,re
from pathlib import Path
from read_benchmark_digits import read
p=argparse.ArgumentParser(description=__doc__);p.add_argument('images',type=Path,nargs='+');a=p.parse_args();out=[]
for path in a.images:
 m=re.search(r'(?:fdiv|fmac|fmac-ram)-(chair|desk|pvm|deskset)-([0123])-([ab])',path.name)
 if not m:p.error(f'bad capture name: {path}')
 g,r,v=read(path,24,41,3);n=read(path,24,59,1)[0];c=read(path,120,59,1)[0]
 if not n or not r or not c:p.error(f'inactive fixture or incomplete 64-sample average: {path}')
 out.append(dict(capture=str(path),fixture=m[1],mode=int(m[2]),repeat=m[3],grid_ticks=g,render_ticks=r,vblank100=v,primary_model_vertices=n,primary_furniture_ticks=c))
print(json.dumps(out,indent=2))
