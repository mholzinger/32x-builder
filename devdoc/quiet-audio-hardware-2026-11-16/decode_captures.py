# Exact project-font readout, including the line table's doubled even rows.
# Refuses ambiguous/non-exact matches; no estimated OCR values enter the CSV.
from pathlib import Path
from PIL import Image
import re
from collections import Counter
import csv,sys
root=Path(__file__).resolve().parents[2]
folder=Path(__file__).resolve().parent
writer=csv.writer(sys.stdout)
writer.writerow(['scene','resolution','arm','frames','ceiling_ticks_mean','render_ticks_mean','delivered_vblanks_mean_x100','wall_ticks_mean','lut_ticks_mean','capture'])
text=(root/'sh_src/font.c').read_text()
glyph={key:[int(x.strip(),16) for x in body.split(',') if x.strip()] for key,body in re.findall(r'/\*\s*([A-Z0-9])\s*\*/\s*\{([^}]+)\}',text)}
glyph[' ']=[0]*8;glyph[':']=[0,96,96,0,96,96,0,0]
for p in sorted(folder.glob('20261116_*quiet-*.png')):
 half='quiet-half' in p.name
 im=Image.open(p).convert('RGB');arm=int(re.search(r'-a(\d)',p.name)[1]);scene=int(re.search(r'-s(\d)',p.name)[1]);label=f'BENCH S:{scene} DIV:1 Q:{arm}'
 colors=[c for c,_ in Counter(im.crop((0,30,240,70)).getdata()).most_common(12)]
 def mismatch(ch,x,y,dy,color):
  err=0
  for yy in range(y,y+8):
   row=((yy&~1) if half else yy)-y
   if not 0<=row<8:continue
   bits=glyph[ch][row]
   for xx in range(8):err+=(im.getpixel((x+xx,yy+dy))==color)!=bool(bits&(128>>xx))
  return err
 fits=[]
 for color in colors:
  for dy in range(17):fits.append((sum(mismatch(ch,8+i*8,32,dy,color) for i,ch in enumerate(label)),dy,color))
 err,dy,color=min(fits)
 assert err==0, (p,err)
 vals=[];errors=[]
 for k in range(3):
  word=''
  for j in range(5):
   fits=sorted((mismatch(ch,8+(k*8+2+j)*8,41,dy,color),ch) for ch in '0123456789')
   assert fits[0][0]==0 and fits[1][0]>0,(p,fits)
   word+=fits[0][1];errors.append(fits[0][0])
  vals.append(int(word))
 wall=[]
 for k in range(2):
  word=''
  for j in range(5):
   fits=sorted((mismatch(ch,8+(k*8+2+j)*8,50,dy,color),ch) for ch in '0123456789')
   assert fits[0][0]==0 and fits[1][0]>0,(p,fits)
   word+=fits[0][1]
  wall.append(int(word))
 assert sum(errors)==0,(p,errors)
 writer.writerow([scene,'half' if half else 'full',arm,64,*vals,*wall,p.name])
