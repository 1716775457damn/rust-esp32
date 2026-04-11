import os
import sys
import subprocess

def install_pydub():
    try:
        import pydub
    except ImportError:
        print("正在安装 pydub...")
        subprocess.check_call([sys.executable, "-m", "pip", "install", "pydub"])

def convert_audio(src, dst):
    from pydub import AudioSegment
    print(f"正在处理: {src} -> {dst}")
    try:
        # 加载音频并转换为单声道、16kHz
        audio = AudioSegment.from_file(src)
        audio = audio.set_frame_rate(16000).set_channels(1).set_sample_width(2)
        audio.export(dst, format="wav")
        print(f"✅ 转换成功: {dst}")
    except Exception as e:
        print(f"❌ 转换失败: {e}")

if __name__ == "__main__":
    install_pydub()
    # 源代码文件路径 (用户上传的位置)
    src_1 = r"e:\GitHub\rust-esp32\1.mp3"
    src_2 = r"e:\GitHub\rust-esp32\2.mp3"
    
    # 目标路径 (工程 SPIFFS 数据集)
    dst_dir = "spiffs_data"
    if not os.path.exists(dst_dir):
        os.makedirs(dst_dir)
        
    dst_1 = os.path.join(dst_dir, "shuffle.wav")
    dst_2 = os.path.join(dst_dir, "draw.wav")
    
    convert_audio(src_1, dst_1)
    convert_audio(src_2, dst_2)
    print("\n🚀 转换任务完成！现在可以运行 prepare_assets.py 了。")
