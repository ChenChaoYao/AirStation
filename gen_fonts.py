import os
import subprocess

chars = set()
for root, dirs, files in os.walk('/home/dannis/文件/PlatformIO/Projects/BTPower/src'):
    for file in files:
        if file.endswith(('.c', '.cpp', '.h')) and not file.startswith('ui_font_chinese'):
            path = os.path.join(root, file)
            with open(path, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
                for ch in content:
                    if ord(ch) > 127:
                        chars.add(ch)

extra = '零一二三四五六七八九十百千萬億二氧化碳揮發性有機物甲醛濃度電壓電流功率溫度濕度大氣壓力海拔高度空氣品質良好正常警戒超標危險監督站系統狀態資訊設定上限更新確認返回主選單微克毫克升米台個節點運行時間封包計數遙測無線連接開關數值°Cμg%±℃'
for ch in extra:
    chars.add(ch)

ascii_chars = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz ~!@#$%^&*()-_=+[{]}\\|;:\'",<.>/?'
all_symbols = ''.join(sorted(list(chars))) + ascii_chars

print('Total symbols length:', len(all_symbols))

font_path = '/usr/share/fonts/open-chinese-fonts/hei.ttf'
for size in [16, 20]:
    out_file = f'src/ui_font_chinese_{size}.c'
    cmd = [
        'npx', '--yes', 'lv_font_conv',
        '--font', font_path,
        '--bpp', '4',
        '--size', str(size),
        '--format', 'lvgl',
        '--no-compress',
        '--no-prefilter',
        '--symbols', all_symbols,
        '-o', out_file
    ]
    print(f'Generating {out_file}...')
    res = subprocess.run(cmd, cwd='/home/dannis/文件/PlatformIO/Projects/BTPower', capture_output=True, text=True)
    if res.returncode != 0:
        print(f'Error generating {size}:', res.stderr)
    else:
        print(f'Successfully generated {out_file}')

    full_out = os.path.join('/home/dannis/文件/PlatformIO/Projects/BTPower', out_file)
    with open(full_out, 'r', encoding='utf-8') as f:
        src = f.read()
    src = src.replace('#include "lvgl/lvgl.h"', '#include "lvgl.h"')
    with open(full_out, 'w', encoding='utf-8') as f:
        f.write(src)
    print(f'Fixed include in {out_file}')
