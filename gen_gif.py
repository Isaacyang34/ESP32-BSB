import sys
import os
from PIL import Image, ImageDraw

width, height = 64, 32
frames = []

BG = (0, 0, 0)
WHITE = (255, 255, 255)
YELLOW = (255, 255, 0)
RED = (255, 50, 50)
ORANGE = (255, 150, 0)
CYAN = (0, 255, 255)
DARK_CYAN = (0, 100, 100)

for i in range(8):
    img = Image.new('RGB', (width, height), BG)
    draw = ImageDraw.Draw(img)
    
    # === 畫球拍拍面 (Zoom In 視角，位於畫面左側) ===
    if i < 3 or i > 4:
        # 平靜狀態的直線網線
        draw.line((15, 0, 15, 32), fill=CYAN, width=2)
        # 橫向網線
        for y in range(2, 32, 6):
            draw.line((10, y, 20, y), fill=DARK_CYAN, width=1)
    elif i == 3:
        # 擊球瞬間！網線往左後方凹陷
        draw.arc((-10, 0, 20, 32), start=90, end=270, fill=CYAN, width=2)
        for y in range(2, 32, 6):
            draw.line((0, y, 10, y), fill=DARK_CYAN, width=1)
    elif i == 4:
        # 網線強力反彈，往右凸出
        draw.arc((10, 0, 40, 32), start=270, end=90, fill=CYAN, width=2)
        for y in range(2, 32, 6):
            draw.line((15, y, 25, y), fill=DARK_CYAN, width=1)
            
    # === 畫羽毛球 (巨大化 Zoom In) ===
    if i == 0:
        # 從遠方飛來 (小)
        cx, cy = 55, 16
        draw.ellipse((cx-2, cy-2, cx+2, cy+2), fill=WHITE) # 球頭
        draw.polygon([(cx+1, cy-3), (cx+1, cy+3), (cx+8, cy+6), (cx+8, cy-6)], outline=WHITE) # 羽毛
    elif i == 1:
        # 接近中 (中)
        cx, cy = 40, 16
        draw.ellipse((cx-4, cy-4, cx+4, cy+4), fill=WHITE)
        draw.polygon([(cx+2, cy-5), (cx+2, cy+5), (cx+12, cy+10), (cx+12, cy-10)], outline=WHITE)
    elif i == 2:
        # 即將撞擊 (大)
        cx, cy = 25, 16
        draw.ellipse((cx-6, cy-6, cx+6, cy+6), fill=WHITE)
        draw.polygon([(cx+3, cy-8), (cx+3, cy+8), (cx+18, cy+15), (cx+18, cy-15)], outline=WHITE)
    elif i == 3:
        # 撞擊！球頭被壓扁，巨大的爆炸火花
        cx, cy = 5, 16
        draw.ellipse((cx-2, cy-10, cx+4, cy+10), fill=WHITE) # 壓扁的球頭
        
        # 滿版的衝擊波火花
        draw.ellipse((cx-5, cy-15, cx+25, cy+15), outline=YELLOW, width=2)
        draw.line((cx+5, cy, 64, -10), fill=RED, width=2)
        draw.line((cx+5, cy, 64, 16), fill=YELLOW, width=4)
        draw.line((cx+5, cy, 64, 42), fill=ORANGE, width=2)
        draw.line((cx+5, cy, 50, 5), fill=WHITE, width=1)
        draw.line((cx+5, cy, 50, 27), fill=WHITE, width=1)
    elif i == 4:
        # 反彈射出
        cx, cy = 25, 16
        # 球頭現在朝向右邊
        draw.ellipse((cx-4, cy-4, cx+4, cy+4), fill=WHITE)
        # 羽毛在左邊
        draw.polygon([(cx-2, cy-5), (cx-2, cy+5), (cx-15, cy+10), (cx-15, cy-10)], outline=WHITE)
        # 原地的殘留衝擊波
        draw.ellipse((10, -5, 30, 37), outline=ORANGE, width=1)
    elif i == 5:
        # 極速飛走
        cx, cy = 50, 16
        draw.ellipse((cx-4, cy-4, cx+4, cy+4), fill=WHITE)
        draw.polygon([(cx-2, cy-5), (cx-2, cy+5), (cx-15, cy+8), (cx-15, cy-8)], outline=WHITE)
        # 速度線殘影
        draw.line((cx-20, cy-3, cx-45, cy-3), fill=ORANGE, width=2)
        draw.line((cx-18, cy+3, cx-45, cy+3), fill=RED, width=2)
        draw.line((cx-25, cy, cx-50, cy), fill=YELLOW, width=1)
    elif i == 6:
        # 飛出畫面，留下尾跡
        draw.line((30, 13, 10, 13), fill=RED, width=1)
        draw.line((40, 16, 10, 16), fill=ORANGE, width=2)
        draw.line((30, 19, 10, 19), fill=RED, width=1)

    frames.append(img)

# 儲存為 GIF，增加一點速度感
output_path = os.path.join(os.path.dirname(__file__), 'smash_zoom.gif')
frames[0].save(output_path, save_all=True, append_images=frames[1:], optimize=False, duration=80, loop=0)
print(f"成功生成動畫：{output_path}")
