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
    
    if i == 0:
        # === 1. 遠觀 (Wide View) ===
        # 人物與球拍
        draw.line((15, 32, 15, 20), fill=WHITE, width=1) # 身體
        draw.line((15, 20, 22, 14), fill=WHITE, width=1) # 舉起的手臂
        draw.line((22, 14, 25, 8), fill=CYAN, width=1)   # 拍桿
        draw.ellipse((23, 2, 29, 8), outline=CYAN)       # 拍框
        # 羽球 (遠處高空飛來)
        cx, cy = 45, 4
        draw.ellipse((cx-1, cy-1, cx+1, cy+1), fill=WHITE)
        draw.polygon([(cx+1, cy-2), (cx+1, cy+2), (cx+5, cy-4), (cx+5, cy-8)], outline=WHITE)
        
    elif i == 1:
        # === 2. Zoom In 1 (拉近) ===
        draw.line((10, 40, 10, 20), fill=WHITE, width=2) # 身體
        draw.line((10, 20, 20, 10), fill=WHITE, width=2) # 手臂
        draw.line((20, 10, 28, 0), fill=CYAN, width=2)   # 拍桿
        draw.ellipse((22, -10, 40, 10), outline=CYAN, width=2) # 變大的拍框
        # 羽球
        cx, cy = 45, 8
        draw.ellipse((cx-2, cy-2, cx+2, cy+2), fill=WHITE)
        draw.polygon([(cx+1, cy-3), (cx+1, cy+3), (cx+8, cy-4), (cx+8, cy-10)], outline=WHITE)
        
    elif i == 2:
        # === 3. Zoom In 2 (超大特寫) ===
        # 巨大的拍框佔滿畫面左側
        draw.ellipse((-15, -20, 35, 52), outline=CYAN, width=2)
        for y in range(-10, 50, 8):
            draw.line((-10, y, 30, y), fill=DARK_CYAN, width=1)
        # 巨大的羽球
        cx, cy = 35, 14
        draw.ellipse((cx-4, cy-4, cx+4, cy+4), fill=WHITE)
        draw.polygon([(cx+2, cy-6), (cx+2, cy+6), (cx+14, cy-2), (cx+14, cy-14)], outline=WHITE)
        
    elif i == 3:
        # === 4. 擊球瞬間 (Impact) ===
        # 網線極度向左後方凹陷
        draw.arc((-15, -10, 25, 42), start=90, end=270, fill=CYAN, width=2)
        for y in range(-10, 40, 6):
            draw.line((-5, y, 10, y), fill=DARK_CYAN, width=1)
            
        cx, cy = 5, 16
        draw.ellipse((cx-2, cy-10, cx+4, cy+10), fill=WHITE) # 球頭被壓扁
        
        # 巨大爆炸特效
        draw.ellipse((cx-5, cy-15, cx+25, cy+15), outline=YELLOW, width=2)
        draw.line((cx, cy, 60, -5), fill=RED, width=2)
        draw.line((cx, cy, 64, 16), fill=YELLOW, width=4)
        draw.line((cx, cy, 50, 40), fill=ORANGE, width=2)

    elif i == 4:
        # === 5. 反彈並斜線射出 (Diagonal Shot) ===
        # 網線反彈往右凸
        draw.arc((10, -10, 40, 42), start=270, end=90, fill=CYAN, width=2)
        
        # 羽球斜線往下飛 (右下角)
        cx, cy = 25, 22
        draw.ellipse((cx-3, cy-3, cx+3, cy+3), fill=WHITE)
        draw.polygon([(cx-2, cy-4), (cx-4, cy+2), (cx-14, cy-4), (cx-10, cy-12)], outline=WHITE) # 羽毛朝左上
        
        # 拖曳火光 (從撞擊點延伸過來)
        draw.line((5, 16, cx, cy), fill=YELLOW, width=3)
        draw.line((2, 13, cx-2, cy-3), fill=ORANGE, width=2)
        draw.line((8, 19, cx-2, cy+2), fill=RED, width=2)

    elif i == 5:
        # === 6. 斜線極速飛行 (Fire Trail) ===
        cx, cy = 50, 30
        draw.ellipse((cx-3, cy-3, cx+3, cy+3), fill=WHITE)
        
        # 拖曳火光持續
        draw.line((15, 19, cx, cy), fill=YELLOW, width=3)
        draw.line((13, 16, cx-2, cy-3), fill=ORANGE, width=2)
        draw.line((17, 22, cx-2, cy+2), fill=RED, width=2)
        
    elif i == 6:
        # === 7. 飛出畫面，殘留火光消散 ===
        draw.line((35, 25, 70, 36), fill=YELLOW, width=2)
        draw.line((30, 22, 65, 33), fill=ORANGE, width=2)
        draw.line((40, 28, 75, 39), fill=RED, width=1)
        
    elif i == 7:
        # === 8. 火光完全消散 ===
        draw.line((50, 30, 85, 41), fill=ORANGE, width=1)
        draw.line((55, 32, 90, 43), fill=RED, width=1)

    frames.append(img)

# 儲存為 GIF
output_path = os.path.join(os.path.dirname(__file__), 'smash_cinematic.gif')
frames[0].save(output_path, save_all=True, append_images=frames[1:], optimize=False, duration=90, loop=0)
print(f"成功生成動畫：{output_path}")
