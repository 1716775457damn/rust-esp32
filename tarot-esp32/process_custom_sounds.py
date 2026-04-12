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
    # 确保依赖库存在
    install_pydub()
    
    # 统一源文件路径
    src_file = r"e:\GitHub\rust-esp32\1.MP3"
    
    # 目标路径
    dst_dir = "spiffs_data"
    if not os.path.exists(dst_dir):
        os.makedirs(dst_dir)
        
    # 同时更新洗牌和抽卡音效
    dst_files = ["shuffle.wav", "draw.wav"]
    
    print(f"🚀 正在基于 {src_file} 更新所有项目音效...")
    
    for filename in dst_files:
        dst_path = os.path.join(dst_dir, filename)
        # 如果旧文件存在则先移除
        if os.path.exists(dst_path):
            os.remove(dst_path)
        convert_audio(src_file, dst_path)
        
    print("\n✨ 音频资产更新完成！")
    print("现在您可以运行 'python prepare_assets.py' 来生成新的 SPIFFS 映像。")
