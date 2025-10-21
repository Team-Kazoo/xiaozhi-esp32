# USB Audio Stream - USB音频流传输

## 概述

USB Audio Stream 模块允许将ESP32的麦克风音频数据通过USB CDC（虚拟串口）实时传输到电脑端，用于音频分析、处理或录制。

## 支持的硬件

- **ESP32-C3** 系列（使用USB Serial/JTAG）
- **ESP32-S2/S3** 系列（使用USB Serial/JTAG）

本模块专为**Xmini C3 V3**开发板设计，使用ES8311音频编解码器。

## 功能特性

- ✅ 实时音频流传输（延迟 < 30ms）
- ✅ 二进制帧格式，带序列号和校验和
- ✅ 自动检测丢帧
- ✅ 支持单声道/立体声（自动转换为单声道）
- ✅ 可配置采样率和帧大小
- ✅ Python接收脚本，支持实时处理和WAV录制

## 使用方法

### ESP32端

#### 1. 添加到你的代码

```cpp
#include "audio/usb_audio_stream.h"
#include "boards/common/board.h"

void app_main() {
    // 获取音频编解码器
    Board& board = Board::GetInstance();
    AudioCodec* codec = board.GetAudioCodec();
    codec->Start();
    
    // 创建USB音频流
    UsbAudioStream usb_stream;
    
    // 初始化（16kHz, 160采样点/帧 = 10ms）
    usb_stream.Initialize(codec, 16000, 160);
    
    // 开始传输
    usb_stream.Start();
    
    // ... 音频数据自动通过USB串口发送 ...
    
    // 停止传输
    usb_stream.Stop();
}
```

#### 2. 使用示例代码

我们提供了一个完整的示例，包含按钮控制：

```cpp
#include "usb_audio_example.h"

void app_main() {
    // 启动USB音频流示例
    // 按BOOT按钮切换音频流开关
    usb_audio_example_start();
}
```

#### 3. 编译和烧录

```bash
# 进入项目目录
cd xiaozhi-esp32

# 配置为xmini-c3-v3板型
idf.py set-target esp32c3

# 编译
idf.py build

# 烧录
idf.py flash
```

### 电脑端

#### 1. 安装依赖

```bash
pip install pyserial numpy soundfile
```

#### 2. 运行接收脚本

**实时监控模式：**
```bash
# Linux/Mac
python3 scripts/usb_audio_receiver.py --port /dev/ttyACM0 --realtime

# Windows
python scripts/usb_audio_receiver.py --port COM3 --realtime
```

**录制到WAV文件：**
```bash
# 录制10秒
python3 scripts/usb_audio_receiver.py --port /dev/ttyACM0 --duration 10 --output audio.wav
```

**指定采样率：**
```bash
# 如果ESP32使用24kHz采样率
python3 scripts/usb_audio_receiver.py --port /dev/ttyACM0 --rate 24000 --realtime
```

#### 3. 查找串口设备

**Linux/Mac:**
```bash
ls /dev/tty* | grep -E "(ACM|USB)"
# 通常是 /dev/ttyACM0 或 /dev/ttyUSB0
```

**Windows:**
- 打开设备管理器
- 查找"端口 (COM 和 LPT)"
- 找到"USB Serial Device (COMx)"

## 帧格式说明

每个音频帧的二进制格式：

```
+--------+--------+--------+----------------+----------+
| Magic  | SeqNum | Length | Audio Data     | Checksum |
| 2 bytes| 2 bytes| 2 bytes| Length bytes   | 2 bytes  |
+--------+--------+--------+----------------+----------+
  0xAA55   0-65535  N bytes  int16 samples    sum&0xFFFF
```

- **Magic**: 固定为 `0xAA55`，用于帧同步
- **SeqNum**: 序列号（0-65535循环），用于检测丢帧
- **Length**: 音频数据长度（字节数）
- **Audio Data**: 16-bit PCM音频采样点（小端序）
- **Checksum**: 简单校验和（前面所有字节的和，取低16位）

## 性能参数

### 带宽占用

采样率：16 kHz，16-bit，单声道
- 音频数据率：32 KB/s
- 加上帧头和校验：约 34 KB/s
- USB CDC可用带宽：~1 MB/s
- **带宽占用：< 5%**

24 kHz采样率：
- 音频数据率：48 KB/s
- 总数据率：约 51 KB/s
- **带宽占用：< 6%**

### 延迟

- 音频帧缓冲：10-20 ms
- USB传输：1-2 ms
- Python处理：< 5 ms
- **总延迟：约 20-30 ms**

### CPU占用

- 音频数据复制：< 1%
- USB传输（DMA）：< 1%
- **总CPU占用：< 2%**

## 自定义音频处理

Python端可以轻松添加自定义处理：

```python
from usb_audio_receiver import AudioFrameReceiver
import numpy as np

receiver = AudioFrameReceiver('/dev/ttyACM0', sample_rate=16000)
receiver.open()

try:
    while True:
        frame = receiver.receive_frame()
        if frame is not None:
            # 你的音频处理算法
            # 例如：计算音量
            volume = np.abs(frame).mean()
            print(f"Volume: {volume:.0f}")
            
            # 例如：频谱分析
            fft = np.fft.rfft(frame)
            # ...
            
finally:
    receiver.close()
```

## 注意事项

### 日志输出

⚠️ **重要**：音频流运行时会自动禁用ESP_LOG输出，避免日志干扰音频数据。

如果需要调试日志：
1. 停止音频流
2. 日志会自动重新启用
3. 或者使用独立的UART输出日志（需要额外配置）

### 与其他功能的兼容性

- ✅ 可以与现有的AI语音处理**并行运行**（只是旁路输出音频副本）
- ✅ 不影响WiFi、I2S等其他外设
- ⚠️ 与串口监视器（idf.py monitor）**冲突**（共用同一USB口）

### 电源管理

- USB连接会阻止深度睡眠
- 如需低功耗模式，停止音频流并断开USB

## 故障排查

### 接收不到数据

1. **检查串口是否正确**
   ```bash
   ls -l /dev/ttyACM0  # Linux/Mac
   ```

2. **关闭其他占用串口的程序**
   - idf.py monitor
   - Arduino IDE Serial Monitor
   - 其他终端程序

3. **检查ESP32是否启动音频流**
   - 看LED是否闪烁（如果有指示）
   - 按BOOT按钮尝试切换

### 大量丢帧

1. **降低采样率或增大帧大小**
   ```cpp
   usb_stream.Initialize(codec, 16000, 320);  // 20ms/帧
   ```

2. **关闭Python端的打印输出**（减少延迟）

3. **检查USB线缆质量**

### 校验和错误

- 可能是USB干扰或数据损坏
- 尝试更换USB线或USB端口
- 降低波特率（虽然USB CDC实际不受此影响）

## API参考

### UsbAudioStream类

```cpp
class UsbAudioStream {
public:
    // 初始化
    bool Initialize(AudioCodec* codec, int sample_rate = 16000, int frame_samples = 160);
    
    // 开始/停止传输
    void Start();
    void Stop();
    
    // 查询状态
    bool IsRunning() const;
    
    // 获取统计信息
    struct Statistics {
        uint32_t frames_sent;
        uint32_t bytes_sent;
        uint32_t read_errors;
        uint32_t write_errors;
    };
    Statistics GetStatistics() const;
};
```

### Python AudioFrameReceiver类

```python
class AudioFrameReceiver:
    def __init__(self, port, baudrate=2000000, sample_rate=24000)
    def open(self) -> bool
    def close(self)
    def receive_frame(self) -> np.ndarray  # 返回int16数组或None
    def print_stats(self)
```

## 示例应用

### 1. 实时语音识别

```python
# 结合语音识别库
from usb_audio_receiver import AudioFrameReceiver
import speech_recognition as sr

receiver = AudioFrameReceiver('/dev/ttyACM0')
receiver.open()

recognizer = sr.Recognizer()
audio_buffer = []

while True:
    frame = receiver.receive_frame()
    if frame is not None:
        audio_buffer.append(frame)
        
        # 每秒识别一次
        if len(audio_buffer) >= 100:  # 1秒 @ 10ms/帧
            audio_data = np.concatenate(audio_buffer)
            # ... 识别逻辑 ...
            audio_buffer.clear()
```

### 2. 音频可视化

```python
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation

# 实时波形显示
fig, ax = plt.subplots()
line, = ax.plot([])

def update(frame_data):
    line.set_data(range(len(frame_data)), frame_data)
    return line,

# ... 结合receiver ...
```

### 3. 音频特征提取

```python
import librosa

frame = receiver.receive_frame()
if frame is not None:
    # 转换为float
    audio_float = frame.astype(float) / 32768.0
    
    # 提取MFCC特征
    mfcc = librosa.feature.mfcc(y=audio_float, sr=16000, n_mfcc=13)
    
    # 送给你的算法
    your_algorithm(mfcc)
```

## 许可证

与xiaozhi-esp32项目保持一致。

