import os
import shutil

# 配置路径
src_dir = r"e:\GitHub\rust-esp32\tarot_cards"
dst_dir = r"e:\GitHub\rust-esp32\tarot-esp32\spiffs_data"
map_file = os.path.join(dst_dir, "names.txt")

# Ensure clean destination
if os.path.exists(dst_dir):
    print(f"Cleaning up old assets in {dst_dir}...")
    for f in os.listdir(dst_dir):
        if f.endswith(".wav"): continue # Preserve audio assets
        file_path = os.path.join(dst_dir, f)
        try:
            if os.path.isfile(file_path):
                os.unlink(file_path)
        except Exception as e:
            print(f"Failed to delete {file_path}: {e}")
else:
    os.makedirs(dst_dir)

# 搬运英文库
json_src = r"e:\GitHub\rust-esp32\tarot-esp32\meanings_en.json"
json_dst = os.path.join(dst_dir, "meanings_en.json")
if os.path.exists(json_src):
    shutil.copy(json_src, json_dst)
    print("Successfully synchronized meanings_en.json")

# Get all jpg files
files = [f for f in os.listdir(src_dir) if f.endswith(".jpg")]
files.sort()

mapping = []

for i, filename in enumerate(files):
    # Output name is {i}.bin
    new_name = f"{i}.bin"
    from PIL import Image
    try:
        with Image.open(os.path.join(src_dir, filename)) as img:
            img = img.convert("RGB")
            
            # V4.13: Narrow Width Spec (152x260) - Proportional scale (fit-within)
            # Remove black bars by matching canvas width to card width
            w, h = img.size
            scale = min(152.0 / w, 260.0 / h)
            new_w = int(w * scale)
            new_h = int(h * scale)
            img = img.resize((new_w, new_h), Image.LANCZOS)
            
            # Combine with black canvas (152x260) for HW compatibility/Fixed size
            final_img = Image.new("RGB", (152, 260), (0, 0, 0))
            # Center horizontally, top-aligned vertically
            paste_x = (152 - new_w) // 2
            final_img.paste(img, (paste_x, 0))
            
            img = final_img
            
            # Convert to RGB565 binary
            raw_rgb565 = bytearray()
            pixels = img.getdata()
            for r, g, b in pixels:
                # 5-6-5 conversion
                p = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                raw_rgb565.append((p >> 8) & 0xFF)
                raw_rgb565.append(p & 0xFF)
                
            with open(os.path.join(dst_dir, new_name), "wb") as bf:
                bf.write(raw_rgb565)
                
    except Exception as e:
        print(f"Error processing {filename}: {e}")
    
    display_name = f"card_{i}"
    mapping.append(f"{i}:{display_name}")
    print(f"Processed {i} -> {new_name}")

with open(map_file, "w", encoding="utf-8") as f:
    f.write("\n".join(mapping))

print(f"✅ Successfully processed {len(mapping)} cards (RGB565 Pipeline).")
