"""Rasterize original frog-miner assets for the badge; no runtime WebP decoder."""
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parent
names=['frog-small','frog-big','diamond-small','dandan-head','od-boss','od-menu']+[f'frog-rare-{i}' for i in range(1,48)]
sizes=[(18,18),(27,38),(10,10),(28,28),(32,26),(70,70)]+[(24,27)]*47
out=['#include "apps.h"\n']
for i,(name,size) in enumerate(zip(names,sizes)):
    im=Image.open(ROOT/'ui/app_assets'/f'{name}.webp').convert('RGBA')
    im.thumbnail(size,Image.Resampling.LANCZOS)
    raw=im.tobytes('raw','BGRA')
    out.append(f'static const uint8_t sprite{i}[]={{'+','.join(map(str,raw))+'};\n')
    out.append('static const lv_image_dsc_t image%d={.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_ARGB8888,.w=%d,.h=%d,.stride=%d},.data_size=sizeof(sprite%d),.data=sprite%d};\n'%(i,im.width,im.height,im.width*4,i,i))
out.append('const lv_image_dsc_t *const miner_sprites[53]={'+','.join(f'&image{i}' for i in range(53))+'};\n')
(ROOT/'ui/app_assets.c').write_text(''.join(out),encoding='utf-8')
print('Converted',len(names),'original sprites')
