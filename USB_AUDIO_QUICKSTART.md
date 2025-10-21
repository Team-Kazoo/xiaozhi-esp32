# USB音频流传输 - 快速开始指南

## 简介

这个功能让你可以将ESP32-C3（Xmini C3 V3）的麦克风音频数据通过USB线实时传输到电脑，用于音频分析或处理。

## 5分钟快速开始

### 第一步：准备硬件

- ✅ Xmini C3 V3 开发板
- ✅ USB-C 数据线（连接到电脑）
- ✅ 确保麦克风工作正常

### 第二步：修改代码

打开 `main/main.cc`，在 `app_main()` 函数中添加：

```cpp
#include "usb_audio_example.h"

extern "C" void app_main() {
    // 启动USB音频流
    usb_audio_example_start();
    
    // 按BOOT按钮（GPIO 9）可以切换音频流开关
}
```

或者，如果你想完全控制，使用底层API：

```cpp
#include "audio/usb_audio_stream.h"
#include "boards/common/board.h"
#include "boards/xmini-c3-v3/config.h"

extern "C" void app_main() {
    // 获取板子实例和音频编解码器
    Board& board = Board::GetInstance();
    AudioCodec* codec = board.GetAudioCodec();
    codec->Start();
    
    // 创建并启动USB音频流
    static UsbAudioStream usb_stream;
    usb_stream.Initialize(codec, 24000, 240);  // 24kHz, 10ms/帧
    usb_stream.Start();
    
    // 音频数据开始自动传输...
}
```

### 第三步：编译和烧录

```bash
# 设置目标芯片
idf.py set-target esp32c3

# 编译
idf.py build

# 烧录（会自动检测串口）
idf.py flash
```

### 第四步：电脑端接收

#### 安装Python依赖

```bash
pip install pyserial numpy soundfile
```

#### 运行接收脚本

**Linux/Mac:**
```bash
python3 scripts/usb_audio_receiver.py --port /dev/ttyACM0 --realtime
```

**Windows:**
```bash
python scripts/usb_audio_receiver.py --port COM3 --realtime
```

#### 录制到WAV文件

```bash
# 录制10秒音频
python3 scripts/usb_audio_receiver.py --port /dev/ttyACM0 --duration 10 --output test.wav
```

### 第五步：验证

如果一切正常，你会看到类似输出：

```
Opened serial port: /dev/ttyACM0 @ 2000000 baud

=== Real-time Mode ===
Receiving audio frames... Press Ctrl+C to stop
[1.0s] Frames: 100, Dropped: 0, Buffer: 100 frames
[2.0s] Frames: 200, Dropped: 0, Buffer: 200 frames
[3.0s] Frames: 300, Dropped: 0, Buffer: 300 frames
...
```

**按 Ctrl+C 停止，查看统计信息：**

```
=== Statistics ===
Frames received:  1000
Frames dropped:   0
Checksum errors:  0
Sync errors:      0
Bytes received:   326000
Success rate:     100.00%
```

## 常见问题

### Q: 找不到串口设备

**Linux/Mac:**
```bash
ls /dev/tty* | grep ACM
# 应该显示 /dev/ttyACM0
```

如果没有，检查：
- USB线是否插好
- ESP32是否正常启动
- 是否有权限（`sudo chmod 666 /dev/ttyACM0`）

**Windows:**
- 打开"设备管理器"
- 查看"端口 (COM & LPT)"下的COM号

### Q: 提示端口被占用

关闭这些程序：
- `idf.py monitor`
- Arduino串口监视器
- 其他串口工具

### Q: 收到数据但有大量错误

1. **如果是"sync errors"**：可能是之前有调试日志，等待几秒让缓冲区清空
2. **如果是"checksum errors"**：可能USB线质量问题，换一根
3. **如果是"frames dropped"**：降低采样率或增大帧大小

### Q: 采样率应该设置多少？

- **16 kHz**：最常用，适合语音识别（每帧160采样点 = 10ms）
- **24 kHz**：Xmini C3 V3的原生采样率（每帧240采样点 = 10ms）
- **48 kHz**：高保真音频（需要更高带宽）

建议使用板子的原生采样率（24 kHz），避免重采样开销。

## 下一步

### 自定义Python处理

创建你自己的接收脚本：

```python
from scripts.usb_audio_receiver import AudioFrameReceiver
import numpy as np

# 打开接收器
receiver = AudioFrameReceiver('/dev/ttyACM0', sample_rate=24000)
receiver.open()

try:
    while True:
        # 接收一帧音频
        frame = receiver.receive_frame()
        
        if frame is not None:
            # 你的处理逻辑
            print(f"Received {len(frame)} samples")
            
            # 例如：计算音量
            volume = np.abs(frame).mean()
            if volume > 1000:
                print("🔊 Loud sound detected!")
            
            # 例如：检测频率
            fft = np.fft.rfft(frame)
            dominant_freq = np.argmax(np.abs(fft))
            print(f"Dominant frequency bin: {dominant_freq}")

finally:
    receiver.close()
    receiver.print_stats()
```

### 与其他功能结合

USB音频流可以与项目的AI功能并行运行：

```cpp
// 同时启用AI语音助手和USB音频流
void app_main() {
    // 启动正常的AI助手功能
    Application& app = Application::GetInstance();
    app.Start();
    
    // 同时启动USB音频流（旁路输出）
    usb_audio_example_start();
    
    // 两个功能独立运行，互不干扰
}
```

### 高级配置

调整性能参数：

```cpp
// 低延迟模式（5ms/帧）
usb_stream.Initialize(codec, 24000, 120);

// 标准模式（10ms/帧，推荐）
usb_stream.Initialize(codec, 24000, 240);

// 高带宽模式（20ms/帧）
usb_stream.Initialize(codec, 24000, 480);
```

## 技术支持

详细文档：`main/audio/USB_AUDIO_STREAM.md`

遇到问题？检查：
1. 硬件连接是否正确
2. 串口设备路径是否正确
3. Python依赖是否安装
4. 是否有其他程序占用串口

## 完整代码示例

### ESP32端完整示例

```cpp
// main/main.cc
#include "audio/usb_audio_stream.h"
#include "boards/common/board.h"
#include "button.h"
#include <esp_log.h>

static const char* TAG = "main";
static UsbAudioStream* g_usb_stream = nullptr;
static bool g_streaming = false;

void toggle_stream() {
    if (g_streaming) {
        ESP_LOGI(TAG, "Stopping stream...");
        g_usb_stream->Stop();
        g_streaming = false;
        // 重新启用日志
        esp_log_level_set("*", ESP_LOG_INFO);
        ESP_LOGI(TAG, "Stream stopped");
    } else {
        ESP_LOGI(TAG, "Starting stream in 1 second...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        g_usb_stream->Start();
        g_streaming = true;
    }
}

extern "C" void app_main() {
    // 初始化板子
    Board& board = Board::GetInstance();
    AudioCodec* codec = board.GetAudioCodec();
    codec->Start();
    
    ESP_LOGI(TAG, "Audio codec started");
    
    // 创建USB音频流
    g_usb_stream = new UsbAudioStream();
    g_usb_stream->Initialize(codec, 24000, 240);
    
    // 设置按钮控制
    static Button boot_button(GPIO_NUM_9);
    boot_button.OnClick(toggle_stream);
    
    ESP_LOGI(TAG, "Press BOOT button to toggle USB audio stream");
    ESP_LOGI(TAG, "Ready!");
}
```

### Python端完整示例

```python
#!/usr/bin/env python3
import sys
from scripts.usb_audio_receiver import AudioFrameReceiver
import numpy as np

def main():
    # 配置串口
    port = '/dev/ttyACM0'  # Linux/Mac
    # port = 'COM3'  # Windows
    
    # 创建接收器
    receiver = AudioFrameReceiver(port, sample_rate=24000)
    
    if not receiver.open():
        print("Failed to open port")
        return 1
    
    print("Receiving audio... Press Ctrl+C to stop")
    audio_buffer = []
    
    try:
        while True:
            frame = receiver.receive_frame()
            if frame is not None:
                audio_buffer.append(frame)
                
                # 每100帧处理一次（约1秒）
                if len(audio_buffer) >= 100:
                    audio_data = np.concatenate(audio_buffer)
                    
                    # 你的处理逻辑
                    volume = np.abs(audio_data).mean()
                    print(f"Average volume: {volume:.0f}")
                    
                    audio_buffer.clear()
    
    except KeyboardInterrupt:
        print("\nStopped")
    
    finally:
        receiver.close()
        receiver.print_stats()
    
    return 0

if __name__ == '__main__':
    sys.exit(main())
```

## 开始使用

准备好了吗？开始吧！

```bash
# 1. 编译烧录
idf.py build flash

# 2. 运行Python接收
python3 scripts/usb_audio_receiver.py --port /dev/ttyACM0 --realtime
```

祝你使用愉快！🎤✨

