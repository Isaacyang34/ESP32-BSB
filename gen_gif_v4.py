import sys
import os
import math
from PIL import Image, ImageDraw

# ==========================================
# 調整這裡的數字可以改變播放速度 (單位：毫秒/幀)
# 數值越小 = 動畫越快 (例如：40)
# 數值越大 = 動畫越慢 (例如：150)
FRAME_DURATION = int(sys.argv[1]) if len(sys.argv) > 1 else 90
# ==========================================

width, height = 64, 32
frames = []

BG = (0, 0, 0)
WHITE = (255, 255, 255)
YELLOW = (255, 255, 0)
RED = (255, 50, 50)
ORANGE = (255, 150, 0)
CYAN = (0, 255, 255)
DARK_CYAN = (0, 100, 100)
GREY = (180, 180, 180)

def draw_shuttlecock(draw, cx, cy, angle_deg, scale, comp_x=1.0):
    # angle_deg: 0=朝左, -20=朝左下, 180=朝右, 200=朝右下
    rad = math.radians(angle_deg)
    cos_a = math.cos(rad)
    sin_a = math.sin(rad)
    
    def rot(x, y):
        x = x * comp_x # 壓縮變形
        rx = x * cos_a - y * sin_a
        ry = x * sin_a + y * cos_a
        return cx + rx, cy + ry

    cork_r = 4 * scale
    skirt_l = 15 * scale
    skirt_w = 10 * scale
    
    # 球頭 (用多邊形近似圓形以便旋轉壓縮)
    steps = 12
    cork_points = []
    for step in range(steps):
        a = math.radians(360 * step / steps)
        # 球頭主要在 x <= 0 的區域，稍微超出
        px = math.cos(a) * cork_r - 1
        py = math.sin(a) * cork_r
        cork_points.append(rot(px, py))
    draw.polygon(cork_points, fill=WHITE)
    
    # 羽毛 (裙部)
    p1 = rot(0, -cork_r + 1)
    p2 = rot(skirt_l, -skirt_w)
    p3 = rot(skirt_l, skirt_w)
    p4 = rot(0, cork_r - 1)
    
    draw.polygon([p1, p2, p3, p4], outline=WHITE, fill=None)
    # 羽毛的骨架細節
    draw.line([rot(0,0), rot(skirt_l, -skirt_w*0.5)], fill=GREY, width=1)
    draw.line([rot(0,0), rot(skirt_l, skirt_w*0.5)], fill=GREY, width=1)
    draw.line([rot(0,0), rot(skirt_l, 0)], fill=GREY, width=1)
    draw.line([p2, p3], fill=WHITE, width=1) # 尾部封口

for i in range(6):
    img = Image.new('RGB', (width, height), BG)
    draw = ImageDraw.Draw(img)
    
    if i == 0:
        # === 0. Zoom In (刪除原本的遠觀，直接從大特寫開始) ===
        draw.ellipse((-15, -20, 35, 52), outline=CYAN, width=2)
        # 橫向網線
        for y in range(-10, 50, 6):
            draw.line((-10, y, 30, y), fill=DARK_CYAN, width=1)
        # 新增：垂直網線！
        for x in range(-5, 30, 6):
            draw.line((x, -15, x, 50), fill=DARK_CYAN, width=1)
            
        # 巨大且形狀正確的羽球 (朝左下飛來)
        draw_shuttlecock(draw, cx=40, cy=8, angle_deg=-15, scale=1.0)
        
    elif i == 1:
        # === 1. Impact (斜角撞擊) ===
        draw.arc((-15, -10, 25, 42), start=90, end=270, fill=CYAN, width=2)
        # 變形的網線
        for y in range(-10, 40, 6):
            draw.line((-5, y, 10, y), fill=DARK_CYAN, width=1)
        for x in range(0, 20, 6):
            draw.arc((x-15, -10, x+15, 42), start=90, end=270, fill=DARK_CYAN, width=1)
            
        cx, cy = 8, 16
        # 撞擊時球被嚴重壓扁 (comp_x=0.4)，並帶有微小斜角 (-5度)
        draw_shuttlecock(draw, cx=cx, cy=cy, angle_deg=-5, scale=1.0, comp_x=0.4)
        
        # 爆炸火花也稍微斜向右下角
        draw.ellipse((cx-5, cy-15, cx+25, cy+15), outline=YELLOW, width=2)
        draw.line((cx, cy, 55, 0), fill=RED, width=2)
        draw.line((cx, cy, 64, 24), fill=YELLOW, width=4) # 主爆炸線斜右下
        draw.line((cx, cy, 45, 42), fill=ORANGE, width=2)

    elif i == 2:
        # === 2. Diagonal Shot (斜線反彈) ===
        draw.arc((10, -10, 40, 42), start=270, end=90, fill=CYAN, width=2)
        
        # 羽球朝右下角斜飛 (195度 = 朝右再往下斜15度)
        cx, cy = 25, 20
        draw_shuttlecock(draw, cx=cx, cy=cy, angle_deg=195, scale=0.9, comp_x=0.9)
        
        # 拖曳火光
        draw.line((5, 16, cx, cy), fill=YELLOW, width=3)
        draw.line((2, 13, cx-4, cy-4), fill=ORANGE, width=2)
        draw.line((8, 19, cx-2, cy+2), fill=RED, width=2)

    elif i == 3:
        # === 3. Diagonal Fast ===
        cx, cy = 52, 27
        draw_shuttlecock(draw, cx=cx, cy=cy, angle_deg=195, scale=0.9, comp_x=1.0)
        
        # 拖曳火光持續
        draw.line((15, 17, cx, cy), fill=YELLOW, width=3)
        draw.line((13, 14, cx-4, cy-4), fill=ORANGE, width=2)
        draw.line((17, 20, cx-2, cy+2), fill=RED, width=2)
        
    elif i == 4:
        # === 4. 飛出畫面，殘留火光消散 ===
        draw.line((35, 23, 70, 32), fill=YELLOW, width=2)
        draw.line((30, 20, 65, 29), fill=ORANGE, width=2)
        draw.line((40, 26, 75, 35), fill=RED, width=1)
        
    elif i == 5:
        # === 5. 火光完全消散 ===
        draw.line((50, 27, 85, 36), fill=ORANGE, width=1)
        draw.line((55, 29, 90, 38), fill=RED, width=1)

    frames.append(img)

# 儲存為 GIF
speed_suffix = f"_{FRAME_DURATION}ms" if len(sys.argv) > 1 else ""
output_path = os.path.join(os.path.dirname(__file__), f'smash_v4{speed_suffix}.gif')
frames[0].save(output_path, save_all=True, append_images=frames[1:], optimize=False, duration=FRAME_DURATION, loop=0)
print(f"成功生成動畫：{output_path} (每格停留 {FRAME_DURATION} 毫秒)")
