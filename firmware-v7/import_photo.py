"""Desktop-only decoder: shared UI receives a 360px image descriptor."""
from pathlib import Path
import sys
from PIL import Image, ImageOps

source=Path(sys.argv[1])
if source.stat().st_size>20*1024*1024: raise ValueError('File exceeds 20 MB')
Image.MAX_IMAGE_PIXELS=25_000_000
with Image.open(source) as im:
    if im.width*im.height>25_000_000: raise ValueError('Image exceeds 25 megapixels')
    if im.format not in {'PNG','JPEG','BMP'}: raise ValueError('Unsupported image format')
    ImageOps.fit(ImageOps.exif_transpose(im).convert('RGB'),(360,360),method=Image.Resampling.LANCZOS).save('build/imported.bmp')
