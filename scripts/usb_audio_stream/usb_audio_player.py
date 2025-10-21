#!/usr/bin/env python3
"""
USB Audio Player - 实时播放来自ESP32的音频

依赖：
    pip install pyserial numpy pyaudio
"""

import sys
import argparse
import numpy as np
from usb_audio_receiver import AudioFrameReceiver

try:
    import pyaudio
    HAS_PYAUDIO = True
except ImportError:
    HAS_PYAUDIO = False
    print("Error: pyaudio not installed")
    print("Install with: pip install pyaudio")
    sys.exit(1)


class AudioPlayer:
    """实时音频播放器"""
    
    def __init__(self, sample_rate=24000):
        self.sample_rate = sample_rate
        self.p = pyaudio.PyAudio()
        self.stream = None
        
    def start(self):
        """启动音频流"""
        self.stream = self.p.open(
            format=pyaudio.paInt16,
            channels=1,
            rate=self.sample_rate,
            output=True,
            frames_per_buffer=60  # 2.5ms @ 24kHz - 低延迟模式
        )
        print(f"Audio output started: {self.sample_rate} Hz")
    
    def play(self, audio_data):
        """播放音频数据（int16数组）"""
        if self.stream:
            self.stream.write(audio_data.tobytes())
    
    def stop(self):
        """停止音频流"""
        if self.stream:
            self.stream.stop_stream()
            self.stream.close()
        self.p.terminate()


def main():
    parser = argparse.ArgumentParser(
        description='Real-time audio playback from ESP32 via USB CDC'
    )
    
    parser.add_argument('--port', '-p', required=True,
                        help='Serial port device (e.g., /dev/ttyACM0)')
    parser.add_argument('--rate', '-r', type=int, default=24000,
                        help='Audio sample rate in Hz (default: 24000)')
    
    args = parser.parse_args()
    
    # 创建接收器
    receiver = AudioFrameReceiver(args.port, sample_rate=args.rate)
    
    if not receiver.open():
        return 1
    
    # 创建播放器
    player = AudioPlayer(sample_rate=args.rate)
    player.start()
    
    print("\n=== Real-time Audio Playback ===")
    print("Playing audio from ESP32... Press Ctrl+C to stop\n")
    
    frame_count = 0
    
    try:
        while True:
            # 接收一帧
            frame = receiver.receive_frame()
            
            if frame is not None:
                # 实时播放
                player.play(frame)
                
                frame_count += 1
                if frame_count % 100 == 0:  # 每秒打印一次
                    print(f"Playing... Frames: {frame_count}, "
                          f"Dropped: {receiver.stats['frames_dropped']}")
    
    except KeyboardInterrupt:
        print("\n\nStopped by user")
    
    finally:
        player.stop()
        receiver.close()
        receiver.print_stats()
    
    return 0


if __name__ == '__main__':
    sys.exit(main())

