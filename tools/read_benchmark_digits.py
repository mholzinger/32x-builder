# Exact pixel-template reader for native MiSTer benchmark screenshots.
# --half models the line table sampling even framebuffer rows twice.
from pathlib import Path
from PIL import Image
from collections import Counter
import re,csv
src=(Path(__file__).resolve().parents[1]/'sh_src/font.c').read_text()
glyph={key:[int(v.strip(),16) for v in vals.split(',') if v.strip()] for key,vals in re.findall(r'/\*\s*([0-9])\s*\*/\s*\{([^}]+)\}',src)}
def read(path,x0,y0,groups,half=False):
 im=Image.open(path).convert('RGB');colors=[c for c,_ in Counter(im.crop((x0,y0, min(im.width,x0+groups*64),y0+8)).getdata()).most_common(12)];solutions=[]
 for color in colors:
  vals=[];error=0
  for k in range(groups):
   num=''
   for j in range(5):
    scores=[]
    for ch,rows in glyph.items():
     n=0
     for yy in range(y0,y0+8):
      row=((yy&~1) if half else yy)-y0
      if row<0:continue
      for xx in range(8):
       bit=bool(rows[row]&(128>>xx));n+=(im.getpixel((x0+k*64+j*8+xx,yy))==color)!=bit
     scores.append((n,ch))
    scores.sort()
    if scores[0][0]==scores[1][0]:
     error+=1000
    error+=scores[0][0];num+=scores[0][1]
   vals.append(int(num))
  solutions.append((error,vals,color))
 error,vals,color=min(solutions)
 assert error==0,(path,error,vals)
 return vals
if __name__ == '__main__':
 import argparse,json
 parser=argparse.ArgumentParser(description='Read exact project-font numeric captions; reject non-matches.')
 parser.add_argument('images',nargs='+',type=Path)
 parser.add_argument('--x',type=int,default=24)
 parser.add_argument('--y',type=int,default=41)
 parser.add_argument('--groups',type=int,default=3)
 parser.add_argument('--half',action='store_true')
 args=parser.parse_args()
 for p in args.images:
  print(json.dumps({'capture':str(p),'values':read(p,args.x,args.y,args.groups,args.half)}))
