"""Import the reference firmware's bitmap assets; no Arduino/C++ runtime needed.

Usage: python tools/import-ui-assets.py C:/projects/esp32-div
Generated files are checked in, so normal builds do not need the reference tree.
"""
from pathlib import Path
import re
import sys
import zipfile

root = Path(__file__).resolve().parents[1]
reference = Path(sys.argv[1])
out = root / 'components/div_ui'


def numbers(text):
    text = re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)
    return [int(n, 0) for n in re.findall(r'0x[0-9a-fA-F]+|\b\d+\b', text)]


def array(name, data, ctype='uint8_t'):
    rows = [', '.join(f'0x{x:02x}' for x in data[i:i+16]) for i in range(0, len(data), 16)]
    return f'static const {ctype} {name}[] = {{\n    ' + ',\n    '.join(rows) + '\n};\n'


source = ['/* Generated from ESP32-DIV/icon.h and TFT_eSPI fonts. See ASSET_NOTICES.md. */\n',
          '#include "div_ui_assets.h"\n#include <string.h>\n']
header = ['#pragma once\n#include "lvgl.h"\n',
          'extern const lv_font_t div_font_16, div_font_8;\n']
icons = reference.joinpath('ESP32-DIV/icon.h').read_text(encoding='utf-8')
for name, content in re.findall(r'bitmap_icon_(\w+)\s*\[\s*\]\s*PROGMEM\s*=\s*\{(.*?)\};', icons, re.S):
    data = numbers(content)
    if len(data) != 32:
        continue
    source.append(array(f'pixels_{name}', data))
    source.append(f'const lv_img_dsc_t div_icon_{name} = {{ .header={{.cf=LV_IMG_CF_ALPHA_1BIT,.w=16,.h=16}}, .data_size=32,.data=pixels_{name} }};\n')
    header.append(f'extern const lv_img_dsc_t div_icon_{name};\n')

with zipfile.ZipFile(reference / 'Libraries/TFT_eSPI-master.zip') as archive:
    font16 = archive.read('TFT_eSPI-master/Fonts/Font16.c').decode('utf-8')
    font8 = archive.read('TFT_eSPI-master/Fonts/glcdfont.c').decode('utf-8')

# The reference enables both optional glyphs (dollar and degree).
active, stack, filtered = True, [], []
for line in font16.splitlines():
    if line.startswith('#ifdef'):
        stack.append(active)
    elif line.startswith('#else'):
        active = not active and stack[-1]
    elif line.startswith('#endif'):
        active = stack.pop()
    elif active:
        filtered.append(line)
font16 = re.sub(r'//[^\n]*', '', '\n'.join(filtered))
widths = numbers(re.search(r'widtbl_f16\[96\]\s*=\s*\{(.*?)\}', font16, re.S)[1])
glyphs = {int(code, 16): numbers(data) for code, data in re.findall(r'chr_f16_([\da-fA-F]+)\[\d+\]\s*=\s*\{(.*?)\}', font16, re.S)}
assert len(widths) == 96 and len(glyphs) == 96


def pack(bits):
    result = [0] * ((len(bits)+7)//8)
    for i, bit in enumerate(bits):
        result[i//8] |= bool(bit) << (7-i%8)
    return result


bitmap, offsets = [], []
for ch, width in zip(range(32, 128), widths):
    offsets.append(len(bitmap))
    glyph = glyphs[ch]
    stride = (width+6)//8  # TFT_eSPI includes a trailing blank column in the advance.
    assert len(glyph) == stride*16
    bitmap += pack([glyph[y*stride+x//8] & (128 >> (x%8)) if x < stride*8 else 0
                    for y in range(16) for x in range(width)])
source += [array('font16_widths', widths), array('font16_offsets', offsets, 'uint16_t'), array('font16_bits', bitmap)]
small = numbers(re.search(r'font\[\]\s*PROGMEM\s*=\s*\{(.*?)\}', font8, re.S)[1])
bitmap = []
for ch in range(32, 128):
    bitmap += pack([small[ch*5+x] & (1 << y) if x < 5 else 0 for y in range(8) for x in range(6)])
source.append(array('font8_bits', bitmap))
source.append('''
static bool glyph_info(const lv_font_t *font, lv_font_glyph_dsc_t *dsc, uint32_t ch, uint32_t next)
{
    (void)next;
    if (ch == 0xb0 && font == &div_font_16) ch = 0x60;
    if (ch < 32 || ch > 127) return false;
    memset(dsc, 0, sizeof(*dsc));
    dsc->adv_w = dsc->box_w = font == &div_font_16 ? font16_widths[ch-32] : 6;
    dsc->box_h = font->line_height;
    dsc->bpp = 1;
    return true;
}
static const uint8_t *glyph_bitmap(const lv_font_t *font, uint32_t ch)
{
    if (ch == 0xb0 && font == &div_font_16) ch = 0x60;
    if (ch < 32 || ch > 127) return NULL;
    return font == &div_font_16 ? font16_bits + font16_offsets[ch-32] : font8_bits + (ch-32)*6;
}
const lv_font_t div_font_16 = {.get_glyph_dsc=glyph_info, .get_glyph_bitmap=glyph_bitmap,
    .line_height=16, .base_line=0, .fallback=&lv_font_montserrat_14};
const lv_font_t div_font_8 = {.get_glyph_dsc=glyph_info, .get_glyph_bitmap=glyph_bitmap,
    .line_height=8, .base_line=0, .fallback=&lv_font_montserrat_14};
''')
out.joinpath('ui_assets.c').write_text(''.join(source), encoding='utf-8')
out.joinpath('include/div_ui_assets.h').write_text(''.join(header), encoding='utf-8')
out.joinpath('ASSET_NOTICES.md').write_text(
    '# Reference UI assets\n\n'
    'Menu layout and 16 x 16 icons are from ESP32-DIV/ESP32-DIV.ino and icon.h.\n'
    'Font bitmaps are from the reference project\'s Libraries/TFT_eSPI-master.zip, '
    'TFT_eSPI 2.5.43 by Bodmer: Fonts/Font16.c (Font 2, dollar/degree variants) '
    'and Fonts/glcdfont.c (original Adafruit_GFX 5x7 font). '
    'The importer only changes bitmap storage layout for LVGL; no C++ library is linked.\n\n'
    'ESP32-DIV license:\n\n' + reference.joinpath('LICENSE').read_text(encoding='utf-8'), encoding='utf-8')
print('Imported original icons and Font 1 / Font 2 into C arrays.')
