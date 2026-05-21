import os
from PIL import Image

gif_path = r"C:\Users\Isaac Yang\Desktop\ESP32-BSB\smash_v4.gif"
output_dir = r"C:\Users\Isaac Yang\.gemini\antigravity\brain\5d2dbf37-5205-4c87-a52e-eb7840c95f2f\scratch"

with Image.open(gif_path) as im:
    for i in range(im.n_frames):
        im.seek(i)
        frame_path = os.path.join(output_dir, f"v4_frame_{i}.png")
        # Save as PNG
        im.save(frame_path)
        print(f"Extracted {frame_path}")
