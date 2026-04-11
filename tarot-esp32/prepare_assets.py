import os
import shutil

# 配置路径
src_dir = r"e:\GitHub\rust-esp32\tarot_cards"
dst_dir = r"e:\GitHub\rust-esp32\tarot-esp32\spiffs_data"
map_file = os.path.join(dst_dir, "names.txt")

if not os.path.exists(dst_dir):
    os.makedirs(dst_dir)

# 获取所有 jpg 文件
files = [f for f in os.listdir(src_dir) if f.endswith(".jpg")]
files.sort()

mapping = []

for i, filename in enumerate(files):
    new_name = f"{i}.jpg"
    # 使用 Pillow 转换为 Baseline JPEG，确保硬件解码器兼容
    from PIL import Image
    try:
        with Image.open(os.path.join(src_dir, filename)) as img:
            img = img.convert("RGB")
            # 强制调整比例到 360x360，避免因尺寸不对导致的硬件解码/渲染异常
            img = img.resize((360, 360), Image.LANCZOS)
            img.save(os.path.join(dst_dir, new_name), "JPEG", quality=90, progressive=False, subsampling=2)
    except Exception as e:
        print(f"Error processing {filename}: {e}")
        shutil.copy(os.path.join(src_dir, filename), os.path.join(dst_dir, new_name))
    
    # 记录映射 (中文名去掉 .jpg)
    display_name = os.path.splitext(filename)[0]
    mapping.append(f"{i}:{display_name}")
    print(f"Processed {i}: {display_name}")

# 写映射文件
with open(map_file, "w", encoding="utf-8") as f:
    f.write("\n".join(mapping))

print(f"Successfully processed {len(mapping)} cards.")
