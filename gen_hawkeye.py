import sys
import os
from PIL import Image, ImageDraw

width, height = 64, 32
frames = []
durations = []

# 顏色定義
BG_GREEN = (34, 139, 34) # 經典球場綠
WHITE = (240, 240, 240)
GREY = (180, 180, 180)
DARK_GREY = (100, 100, 100)
RED = (220, 30, 30)

def draw_out(draw, ox, oy):
    # O
    draw.rectangle((ox, oy, ox+5, oy+7), outline=WHITE, width=2)
    # U
    draw.line((ox+9, oy, ox+9, oy+7), fill=WHITE, width=2)
    draw.line((ox+14, oy, ox+14, oy+7), fill=WHITE, width=2)
    draw.line((ox+9, oy+6, ox+14, oy+6), fill=WHITE, width=2)
    # T
    draw.line((ox+18, oy, ox+24, oy), fill=WHITE, width=2)
    draw.line((ox+21, oy, ox+21, oy+7), fill=WHITE, width=2)

def add_frame(r, show_out, duration_ms):
    img = Image.new('RGB', (width, height), BG_GREEN)
    draw = ImageDraw.Draw(img)
    
    # 畫白色球場線 (參考鷹眼挑戰圖的T字線)
    # 水平底線
    draw.rectangle((0, 14, 64, 18), fill=WHITE)
    # 垂直中線
    draw.rectangle((16, 18, 20, 32), fill=WHITE)
    
    # 畫落點標記 (鷹眼灰色圓點)
    if r > 0:
        # 將圓點中心設定在白線「外側」(也就是上方)，y=8
        cx, cy = 35, 8
        draw.ellipse((cx-r, cy-r, cx+r, cy+r), fill=GREY)
        draw.ellipse((cx-r/2, cy-r/2, cx+r/2, cy+r/2), fill=DARK_GREY)
        
    # 畫 OUT 判定框
    if show_out:
        # 省略 COURT 1，直接畫大大的 OUT 閃爍框
        draw.rectangle((26, 20, 62, 30), fill=RED)
        draw_out(draw, 31, 21)
        
    frames.append(img)
    durations.append(duration_ms)

# === 動畫時間軸編排 ===
# 0. 空白場地等一下
add_frame(r=0, show_out=False, duration_ms=400)
# 1. 灰色圓點由小到大出現 (完全不蓋到白線)
add_frame(r=1, show_out=False, duration_ms=100)
add_frame(r=2, show_out=False, duration_ms=100)
add_frame(r=3, show_out=False, duration_ms=100)
add_frame(r=4, show_out=False, duration_ms=400)
# 2. OUT 紅底白字閃爍判定
add_frame(r=4, show_out=True,  duration_ms=400)
add_frame(r=4, show_out=False, duration_ms=150)
add_frame(r=4, show_out=True,  duration_ms=400)
add_frame(r=4, show_out=False, duration_ms=150)
add_frame(r=4, show_out=True,  duration_ms=1500) # 最後定格久一點

output_path = os.path.join(os.path.dirname(__file__), 'hawkeye_out.gif')
frames[0].save(output_path, save_all=True, append_images=frames[1:], optimize=False, duration=durations, loop=0)
print(f"成功生成鷹眼挑戰動畫：{output_path}")
