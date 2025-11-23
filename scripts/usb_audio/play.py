#!/usr/bin/env python3
"""
USB Audio Player - Real-time audio playback

Usage:
    python play.py --port /dev/ttyACM0
"""

import sys
import argparse
from receiver import AudioFrameReceiver

try:
    import pyaudio
    HAS_PYAUDIO = True
except ImportError:
    HAS_PYAUDIO = False
    print("Error: pyaudio not installed")
    print("Install portaudio first:")
    print("  macOS: brew install portaudio")
    print("  Linux: apt-get install portaudio19-dev")
    print("Then: pip install pyaudio")
    sys.exit(1)


class AudioPlayer:
    """Real-time audio player"""
    
    def __init__(self, sample_rate=48000):
        self.sample_rate = sample_rate
        self.p = pyaudio.PyAudio()
        self.stream = None
        
    def start(self):
        """Start audio stream"""
        self.stream = self.p.open(
            format=pyaudio.paInt16,
            channels=1,
            rate=self.sample_rate,
            output=True,
            frames_per_buffer=120  # Low latency mode (2.5ms @ 48kHz)
        )
        print(f"Audio playback started: {self.sample_rate} Hz")
    
    def play(self, audio_data):
        """Play audio data"""
        if self.stream:
            self.stream.write(audio_data.tobytes())
    
    def stop(self):
        """Stop audio stream"""
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
    parser.add_argument('--rate', '-r', type=int, default=48000,
                        help='Audio sample rate (default: 48000)')
    
    args = parser.parse_args()
    
    # Create receiver
    receiver = AudioFrameReceiver(args.port, sample_rate=args.rate)
    
    if not receiver.open():
        return 1
    
    print(f"Opened serial port: {args.port}")
    
    # Create player
    player = AudioPlayer(sample_rate=args.rate)
    player.start()
    
    print("\nPlaying audio... Press Ctrl+C to stop\n")
    
    frame_count = 0
    
    try:
        while True:
            frame = receiver.receive_frame()
            
            if frame is not None:
                player.play(frame)
                
                frame_count += 1
                if frame_count % 100 == 0:
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

