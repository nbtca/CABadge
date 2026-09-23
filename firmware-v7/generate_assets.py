"""Subset OFL Noto Sans SC and rasterize the existing association mark."""
from pathlib import Path
import re
from PIL import Image, ImageFont, ImageDraw

ROOT=Path(__file__).resolve().parent
chars=set(chr(c) for c in range(32,127))
for path in [ROOT/'ui/badge_ui.c', ROOT/'ui/wifi_panel.c', ROOT/'usb_screen/device/src/wifi_service.c', ROOT/'usb_screen/device/src/ble_service.c', ROOT/'usb_screen/device/src/wallpaper_service.c']:
    chars.update(c for c in path.read_text(encoding='utf-8') if 127<ord(c)<65535)
chars.update(c for c in (ROOT/'ui/apps.c').read_text(encoding='utf-8') if 127<ord(c)<65535)
base_chars=set(chars)
grok_chars=set()
for path in [ROOT/'ui/grok.c', ROOT/'ui/grok_data.h']:
    grok_chars.update(c for c in path.read_text(encoding='utf-8') if 127<ord(c)<65535)
chars=sorted(chars,key=ord)
out=['#include "badge_ui.h"\n']
for size in [14,18,24,36,56]:
    chars=sorted(base_chars | (grok_chars if size==18 else set()),key=ord)
    font=ImageFont.truetype(str(ROOT/'vendor/fonts/NotoSansSC.ttf'),size)
    font.set_variation_by_axes([700 if size>=56 else 600 if size>=24 else 500])
    bounds=[font.getbbox(ch,anchor='ls') for ch in chars]
    ascent=max(-box[1] for box in bounds); descent=max(box[3] for box in bounds)
    line_height=max(size+5,ascent+descent+2)
    bitmap=[]; glyphs=['{0}']
    for ch in chars:
        x0,y0,x1,y1=font.getbbox(ch,anchor='ls'); w,h=x1-x0,y1-y0
        im=Image.new('L',(max(1,w),max(1,h)))
        ImageDraw.Draw(im).text((-x0,-y0),ch,font=font,fill=255,anchor='ls')
        glyphs.append('{%d,%d,%d,%d,%d,%d}'%(len(bitmap),round(font.getlength(ch)*16),w,h,x0,-y1))
        bitmap.extend(im.tobytes() if h and w else b'')
    stem=f'f{size}'
    out += [f'static const uint8_t {stem}_bitmap[]={{'+','.join(map(str,bitmap))+'};\n',
            f'static const lv_font_fmt_txt_glyph_dsc_t {stem}_glyphs[]={{'+','.join(glyphs)+'};\n',
            f'static const uint16_t {stem}_unicode[]={{'+','.join(str(ord(c)-32) for c in chars)+'};\n',
            f'static const lv_font_fmt_txt_cmap_t {stem}_map[]={{ {{.range_start=32,.range_length={ord(chars[-1])-31},.glyph_id_start=1,.unicode_list={stem}_unicode,.list_length={len(chars)},.type=LV_FONT_FMT_TXT_CMAP_SPARSE_TINY}} }};\n',
            f'static lv_font_fmt_txt_dsc_t {stem}_dsc={{.glyph_bitmap={stem}_bitmap,.glyph_dsc={stem}_glyphs,.cmaps={stem}_map,.cmap_num=1,.bpp=8}};\n',
            f'const lv_font_t font{size}={{.get_glyph_dsc=lv_font_get_glyph_dsc_fmt_txt,.get_glyph_bitmap=lv_font_get_bitmap_fmt_txt,.line_height={line_height},.base_line={descent+1},.dsc=&{stem}_dsc}};\n']
im=Image.open(ROOT/'ui/logo.png').convert('RGBA').resize((152,152),Image.Resampling.LANCZOS)
# Retain the exact source geometry, rendered in white for the dark display.
alpha=im.getchannel('A'); rgba=Image.new('RGBA',im.size,'white'); rgba.putalpha(alpha)
raw=rgba.tobytes('raw','BGRA')
out.append('static const uint8_t logo_data[]={'+','.join(map(str,raw))+'};\n')
out.append('const lv_image_dsc_t badge_logo={.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_ARGB8888,.w=152,.h=152,.stride=608},.data_size=sizeof(logo_data),.data=logo_data};\n')

# Built-in color-ribbon wallpaper, rasterized from simple geometry at 4x for smooth edges.
ribbons=Image.new('RGBA',(1440,1440),'#100513')
draw=ImageDraw.Draw(ribbons)
for x,y,size,width,color,start,end in [(-42,-48,412,44,'#FF385B',10,310),(-3,-9,334,40,'#FF8C32',25,330),(36,30,256,36,'#FDCE41',45,345),(74,68,180,32,'#B948F4',65,360)]:
    draw.arc((x*4,y*4,(x+size)*4,(y+size)*4),start,end,fill=color,width=width*4)
ribbons=ribbons.resize((360,360),Image.Resampling.LANCZOS)
wallpaper_path=ROOT/'ui/wallpaper.png'
wallpaper=Image.open(wallpaper_path).convert('RGBA').resize((360,360),Image.Resampling.LANCZOS)
for name,asset in [('badge_ribbons',ribbons),('badge_wallpaper',wallpaper)]:
    # Opaque display art: store exactly the LCD's native format, with no alpha conversion per frame.
    assert asset.getchannel('A').getextrema()==(255,255)
    raw=bytearray()
    rgb=asset.convert('RGB').tobytes()
    for r,g,b in zip(rgb[0::3],rgb[1::3],rgb[2::3]):
        value=((r>>3)<<11)|((g>>2)<<5)|(b>>3)
        raw.extend((value&255,value>>8))
    out.append(f'static const uint8_t {name}_data[]={{'+','.join(map(str,raw))+'};\n')
    out.append(f'const lv_image_dsc_t {name}={{.header={{.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=360,.h=360,.stride=720}},.data_size=sizeof({name}_data),.data={name}_data}};\n')
target=ROOT/'ui/assets.c'
content=''.join(out)
if not target.exists() or target.read_text(encoding='utf-8')!=content:
    target.write_text(content,encoding='utf-8')
print('Generated fonts and logo:',len(chars),'glyphs per size')
