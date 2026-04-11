import wave
import struct
import random
import math
import os
import sys
import subprocess

# --- ESP32 音频格式要求 ---
SAMPLE_RATE = 16000
CHANNELS = 1
SAMPWIDTH = 2 # 16-bit
MAX_AMP = 20000 

def ensure_dir():
    if not os.path.exists("spiffs_data"):
        os.makedirs("spiffs_data")

def install_pydub():
    try:
        import pydub
        return True
    except ImportError:
        print("正在尝试安装自定义音频处理库 pydub...")
        try:
            subprocess.check_call([sys.executable, "-m", "pip", "install", "pydub"])
            import pydub
            return True
        except Exception as e:
            print(f"⚠️ 无法安装 pydub ({e})，将回退到合成音模式。")
            return False

def convert_or_generate():
    ensure_dir()
    has_pydub = install_pydub()
    
    mapping = {
        "shuffle.wav": r"../1.mp3",
        "draw.wav": r"../2.mp3"
    }
    
    for wav_name, mp3_path in mapping.items():
        dst = os.path.join("spiffs_data", wav_name)
        
        if has_pydub and os.path.exists(mp3_path):
            from pydub import AudioSegment
            print(f"🎵 发现自定义音频: {mp3_path}，正在精心转换...")
            try:
                audio = AudioSegment.from_file(mp3_path)
                audio = audio.set_frame_rate(SAMPLE_RATE).set_channels(1).set_sample_width(2)
                if len(audio) > 3000:
                    audio = audio[:3000]
                audio.export(dst, format="wav")
                print(f"✅ 转换成功: {dst}")
                continue
            except Exception as e:
                print(f"❌ 转换失败: {e}，将生成合成音作为后备。")
        else:
            if not has_pydub:
                print(f"提示: 未安装 pydub，无法直接转换 MP3。")
            else:
                print(f"提示: 未找到文件 {mp3_path}。")

        print(f"🎹 正在生成高辨识度合成音 (无 Numpy 模式): {dst}")
        duration = 1.0 if "shuffle" in wav_name else 0.3
        num_samples = int(SAMPLE_RATE * duration)
        
        with wave.open(dst, 'w') as wav_file:
            wav_file.setnchannels(CHANNELS)
            wav_file.setsampwidth(SAMPWIDTH)
            wav_file.setframerate(SAMPLE_RATE)
            
            for i in range(num_samples):
                t = i / SAMPLE_RATE
                # 诊断用滑音 (叮——)
                freq = 800 - (400 * (i / num_samples))
                tone = math.sin(2 * math.pi * freq * t) * 0.4
                # 基础白噪声
                noise = random.uniform(-1, 1) * 0.4
                
                sample = int((tone + noise) * MAX_AMP)
                sample = max(-32768, min(32767, sample))
                wav_file.writeframes(struct.pack('<h', sample))
                
        print(f"✅ 合成成功: {dst}")

if __name__ == "__main__":
    convert_or_generate()
    print("\n🚀 所有音频资源已准备就绪！")
