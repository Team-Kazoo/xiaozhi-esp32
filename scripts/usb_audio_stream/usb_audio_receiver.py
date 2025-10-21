#!/usr/bin/env python3
"""
USB Audio Receiver - 接收来自ESP32的音频流

这个脚本通过USB串口接收来自ESP32的音频数据，解析帧格式，
并可以保存为WAV文件或实时处理。

使用方法：
    python3 usb_audio_receiver.py --port /dev/ttyACM0 --output audio.wav
    python3 usb_audio_receiver.py --port COM3 --realtime

依赖：
    pip install pyserial numpy soundfile
"""

import serial
import struct
import argparse
import numpy as np
import sys
import time
from pathlib import Path

# 尝试导入soundfile用于保存WAV
try:
    import soundfile as sf
    HAS_SOUNDFILE = True
except ImportError:
    HAS_SOUNDFILE = False
    print("Warning: soundfile not installed, WAV saving disabled")
    print("Install with: pip install soundfile")

# 帧格式常量
FRAME_MAGIC = 0xAA55
FRAME_HEADER_SIZE = 6  # magic(2) + seq_num(2) + length(2)
CHECKSUM_SIZE = 2

class AudioFrameReceiver:
    """音频帧接收器"""
    
    def __init__(self, port, baudrate=2000000, sample_rate=24000):
        """
        初始化接收器
        
        Args:
            port: 串口设备路径 (如 /dev/ttyACM0 或 COM3)
            baudrate: 波特率（USB CDC可以设置很高）
            sample_rate: 音频采样率
        """
        self.port = port
        self.baudrate = baudrate
        self.sample_rate = sample_rate
        self.ser = None
        
        # 统计信息
        self.stats = {
            'frames_received': 0,
            'frames_dropped': 0,
            'checksum_errors': 0,
            'sync_errors': 0,
            'bytes_received': 0,
            'last_seq': None,
        }
        
        # 接收缓冲区
        self.buffer = bytearray()
        
    def open(self):
        """打开串口"""
        try:
            self.ser = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                timeout=0.001,  # 1ms超时 - 低延迟模式
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
            )
            print(f"Opened serial port: {self.port} @ {self.baudrate} baud")
            return True
        except Exception as e:
            print(f"Error opening serial port: {e}")
            return False
    
    def close(self):
        """关闭串口"""
        if self.ser:
            self.ser.close()
            print("\nSerial port closed")
    
    def _find_sync(self):
        """在缓冲区中查找帧同步标记"""
        while len(self.buffer) >= 2:
            # 检查前两个字节是否是魔数
            magic = struct.unpack('<H', self.buffer[:2])[0]
            if magic == FRAME_MAGIC:
                return True
            # 不是魔数，丢弃第一个字节继续查找
            self.buffer.pop(0)
            self.stats['sync_errors'] += 1
        return False
    
    def _calculate_checksum(self, data):
        """计算校验和"""
        return sum(data) & 0xFFFF
    
    def receive_frame(self):
        """
        接收一个完整的音频帧
        
        Returns:
            numpy array of int16 audio samples, or None if no frame available
        """
        # 从串口读取数据
        if self.ser and self.ser.in_waiting:
            chunk = self.ser.read(self.ser.in_waiting)
            self.buffer.extend(chunk)
            self.stats['bytes_received'] += len(chunk)
        
        # 查找同步标记
        if not self._find_sync():
            return None
        
        # 检查是否有完整的帧头
        if len(self.buffer) < FRAME_HEADER_SIZE:
            return None
        
        # 解析帧头
        header = struct.unpack('<HHH', self.buffer[:FRAME_HEADER_SIZE])
        magic, seq_num, length = header
        
        # 检查是否有完整的帧（帧头 + 数据 + 校验和）
        frame_size = FRAME_HEADER_SIZE + length + CHECKSUM_SIZE
        if len(self.buffer) < frame_size:
            return None
        
        # 提取完整帧
        frame_data = bytes(self.buffer[:frame_size])
        self.buffer = self.buffer[frame_size:]
        
        # 验证校验和
        received_checksum = struct.unpack('<H', frame_data[-CHECKSUM_SIZE:])[0]
        calculated_checksum = self._calculate_checksum(frame_data[:-CHECKSUM_SIZE])
        
        if received_checksum != calculated_checksum:
            self.stats['checksum_errors'] += 1
            print(f"Checksum error: expected {calculated_checksum:04x}, got {received_checksum:04x}")
            return None
        
        # 检查序列号连续性
        if self.stats['last_seq'] is not None:
            expected_seq = (self.stats['last_seq'] + 1) & 0xFFFF
            if seq_num != expected_seq:
                dropped = (seq_num - expected_seq) & 0xFFFF
                self.stats['frames_dropped'] += dropped
                print(f"Warning: {dropped} frame(s) dropped (seq: {expected_seq} -> {seq_num})")
        
        self.stats['last_seq'] = seq_num
        self.stats['frames_received'] += 1
        
        # 提取音频数据
        audio_data = frame_data[FRAME_HEADER_SIZE:-CHECKSUM_SIZE]
        
        # 转换为int16数组
        samples = np.frombuffer(audio_data, dtype=np.int16)
        
        return samples
    
    def print_stats(self):
        """打印统计信息"""
        print(f"\n=== Statistics ===")
        print(f"Frames received:  {self.stats['frames_received']}")
        print(f"Frames dropped:   {self.stats['frames_dropped']}")
        print(f"Checksum errors:  {self.stats['checksum_errors']}")
        print(f"Sync errors:      {self.stats['sync_errors']}")
        print(f"Bytes received:   {self.stats['bytes_received']}")
        
        if self.stats['frames_received'] > 0:
            success_rate = 100.0 * self.stats['frames_received'] / (
                self.stats['frames_received'] + self.stats['frames_dropped'])
            print(f"Success rate:     {success_rate:.2f}%")


def save_to_wav(filename, audio_data, sample_rate):
    """保存音频数据到WAV文件"""
    if not HAS_SOUNDFILE:
        print("Error: soundfile not installed, cannot save WAV")
        return False
    
    # 转换为float32，范围[-1.0, 1.0]
    audio_float = audio_data.astype(np.float32) / 32768.0
    
    sf.write(filename, audio_float, sample_rate)
    print(f"\nSaved {len(audio_data)} samples to {filename}")
    print(f"Duration: {len(audio_data) / sample_rate:.2f} seconds")
    return True


def realtime_mode(receiver, duration=None):
    """
    实时模式：持续接收并处理音频数据
    
    Args:
        receiver: AudioFrameReceiver实例
        duration: 运行时长（秒），None表示无限运行
    """
    print("\n=== Real-time Mode ===")
    print("Receiving audio frames... Press Ctrl+C to stop")
    
    audio_buffer = []
    start_time = time.time()
    last_print_time = start_time
    
    try:
        while True:
            # 接收一帧
            frame = receiver.receive_frame()
            
            if frame is not None:
                audio_buffer.append(frame)
                
                # 每秒打印一次统计信息
                current_time = time.time()
                if current_time - last_print_time >= 1.0:
                    elapsed = current_time - start_time
                    frames = receiver.stats['frames_received']
                    print(f"[{elapsed:.1f}s] Frames: {frames}, "
                          f"Dropped: {receiver.stats['frames_dropped']}, "
                          f"Buffer: {len(audio_buffer)} frames")
                    last_print_time = current_time
                
                # 这里可以添加实时处理逻辑
                # process_audio(frame)
            
            # 检查是否达到指定时长
            if duration and (time.time() - start_time) >= duration:
                break
                
    except KeyboardInterrupt:
        print("\n\nStopped by user")
    
    # 打印最终统计信息
    receiver.print_stats()
    
    # 返回收集的音频数据
    if audio_buffer:
        return np.concatenate(audio_buffer)
    return np.array([], dtype=np.int16)


def record_mode(receiver, duration, output_file):
    """
    录制模式：接收指定时长的音频并保存
    
    Args:
        receiver: AudioFrameReceiver实例
        duration: 录制时长（秒）
        output_file: 输出文件名
    """
    print(f"\n=== Record Mode ===")
    print(f"Recording for {duration} seconds...")
    
    # 实时接收
    audio_data = realtime_mode(receiver, duration)
    
    # 保存到文件
    if len(audio_data) > 0:
        save_to_wav(output_file, audio_data, receiver.sample_rate)
    else:
        print("Warning: No audio data received")


def main():
    parser = argparse.ArgumentParser(
        description='Receive audio stream from ESP32 via USB CDC',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Record 10 seconds to WAV file
  %(prog)s --port /dev/ttyACM0 --duration 10 --output audio.wav
  
  # Real-time monitoring
  %(prog)s --port COM3 --realtime
  
  # Specify sample rate
  %(prog)s --port /dev/ttyACM0 --rate 24000 --realtime
        """
    )
    
    parser.add_argument('--port', '-p', required=True,
                        help='Serial port device (e.g., /dev/ttyACM0 or COM3)')
    parser.add_argument('--baudrate', '-b', type=int, default=2000000,
                        help='Baud rate (default: 2000000)')
    parser.add_argument('--rate', '-r', type=int, default=24000,
                        help='Audio sample rate in Hz (default: 24000)')
    parser.add_argument('--duration', '-d', type=float,
                        help='Recording duration in seconds')
    parser.add_argument('--output', '-o', type=str,
                        help='Output WAV file name')
    parser.add_argument('--realtime', action='store_true',
                        help='Real-time mode (no file output)')
    
    args = parser.parse_args()
    
    # 检查参数
    if not args.realtime and not args.output:
        parser.error("Either --realtime or --output must be specified")
    
    if args.output and not HAS_SOUNDFILE:
        print("Error: soundfile library required for saving WAV files")
        print("Install with: pip install soundfile")
        return 1
    
    # 创建接收器
    receiver = AudioFrameReceiver(args.port, args.baudrate, args.rate)
    
    if not receiver.open():
        return 1
    
    try:
        if args.output and args.duration:
            # 录制模式
            record_mode(receiver, args.duration, args.output)
        elif args.realtime:
            # 实时模式
            audio_data = realtime_mode(receiver, args.duration)
            # 如果指定了输出文件，保存数据
            if args.output and len(audio_data) > 0:
                save_to_wav(args.output, audio_data, receiver.sample_rate)
        else:
            parser.error("Invalid argument combination")
    
    finally:
        receiver.close()
    
    return 0


if __name__ == '__main__':
    sys.exit(main())

