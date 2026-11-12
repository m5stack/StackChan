"""从 SIL OFL 思源黑体生成看板的常用中文字库。用法：python3 generate_font.py 字体路径"""
from pathlib import Path
import subprocess
import sys

symbols = set(chr(i) for i in range(32, 127))
for first in range(0xA1, 0xF8):
    for second in range(0xA1, 0xFF):
        try:
            symbols.add(bytes([first, second]).decode("gb2312"))
        except UnicodeDecodeError:
            pass
subprocess.run([
    "npx", "--yes", "lv_font_conv@1.5.3", "--font", sys.argv[1],
    "--symbols", "".join(sorted(symbols)), "--size", "14", "--bpp", "2",
    "--no-compress", "--no-kerning", "--format", "lvgl", "--lv-include", "lvgl.h",
    "--lv-font-name", "agent_tasks_cjk_14", "-o", str(Path(__file__).with_name("agent_tasks_cjk_14.c")),
], check=True)
