#!/usr/bin/env python3
"""Decode exact glyphs from native 320x224 SH2ZAM benchmark captures."""
import argparse,json,re
from pathlib import Path
from read_benchmark_digits import read
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('images',type=Path,nargs='+')
args=parser.parse_args()
names=['GENERAL','BATCH','COLD-PREP','WARM-PREP','EXEC-ONLY','MAC-PREP','DMUL-PREP','REUSE','BOX8']
records=[]
for path in args.images:
 m=re.search(r'sh2zam-contrib-(\d+)-([ab])',path.name)
 if not m:parser.error(f'no page/pass in {path}')
 page=int(m[1])
 if page>=27:kernels=['GENERAL','BATCH','MAC-PREP','REUSE-UPD','REUSE-COLD','REUSE-EXEC']
 elif page>=25:kernels=['DUPLICATE','SHARED-UC','SHARED-INV','STALE-PROBE']
 elif page>=21:kernels=['DROP','CLIP-C','CLIP-HW']
 elif page>=17:kernels=['ENGINE','VIEW-C','VIEW-HW','EXEC-ONLY']
 elif page>=9:kernels=['ENGINE','BATCH','COLD-PREP','WARM-PREP','MAC-PREP','DMUL-PREP','REUSE','EXEC-ONLY','MAC-EXACT']
 elif page==8:kernels=['DIV1','DIVU16','SPLIT16','DIVU64']
 else:kernels=names
 record=dict(capture=str(path),page=page,repeat=m[2],count=read(path,128,12,1)[0],
             failures=read(path,48,28,1)[0],reuse=read(path,152,28,1)[0],mac=read(path,240,28,1)[0],kernels={})
 for k,name in enumerate(kernels):
  record['kernels'][name]=dict(zip(['avg_ticks_floor','min_ticks','max_ticks'],
      [read(path,x,68+k*12,1)[0] for x in [112,168,224]]))
 if page in [25,26]:record['expected_stale_failures']=read(path,112,134,1)[0]
 if record['failures']:raise SystemExit(f'correctness failure: {record}')
 records.append(record)
print(json.dumps(records,indent=2))
