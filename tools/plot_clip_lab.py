#!/usr/bin/env python3
"""Render measured clip polygons; no textures, lighting or fake game screenshot."""
from PIL import Image,ImageDraw,ImageFont
from pathlib import Path
import json
root=Path(__file__).resolve().parents[1]
d=root/'devdoc/sh2zam-nightly-2026-11-16';rows=json.loads((d/'clipping-polygons.json').read_text());names=['Chair','Desk','PVM','Desk + PVM']
font_path='/System/Library/Fonts/Helvetica.ttc'
font=ImageFont.truetype(font_path,17);title=ImageFont.truetype(font_path,24)
im=Image.new('RGB',(1060,1610),'white');draw=ImageDraw.Draw(im)
draw.text((30,18),'Near-plane clipping: actual mesh projection diagnostics',fill='#111111',font=title)
draw.text((30,54),'Geometry only — no textures, shadows or painter ordering',fill='#444444',font=font)
for row in rows:
 x=30+row['mode']*520;y=104+row['model']*374
 draw.text((x,y),names[row['model']]+(' — drop crossing faces' if row['mode']==0 else ' — retain clipped polygons'),fill='#222222',font=font)
 panel=Image.new('RGB',(480,336),'#f2f4f6');pd=ImageDraw.Draw(panel,'RGBA')
 for polygon in row['polygons']:
  points=[(int(a*1.5),int(b*1.5)) for a,b in polygon['points']]
  pd.polygon(points,fill=(229,148,53,45) if polygon['clipped'] else (73,124,181,45),outline=(35,50,65,190),width=2)
 im.paste(panel,(x,y+28));draw.rectangle((x,y+28,x+479,y+363),outline='#ccd1d8')
im.save(d/'clipping-diagnostic.png')
