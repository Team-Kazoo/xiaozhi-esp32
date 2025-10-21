# USB音频流 - 硬件调试指南

## ✅ 确认：代码已集成完毕

所有USB音频流代码已正确添加到项目中：

### 已创建的文件（全部已存在）
```
✓ main/audio/usb_audio_stream.h       (2.9 KB)
✓ main/audio/usb_audio_stream.cc      (5.2 KB)
✓ main/usb_audio_example.h            (527 B)
✓ main/usb_audio_example.cc           (3.9 KB)
✓ scripts/usb_audio_receiver.py       (11 KB)
✓ main/CMakeLists.txt                 (已更新，包含新文件)
```

### CMakeLists.txt 已更新
- ✅ 第4行：`"audio/usb_audio_stream.cc"` 已添加
- ✅ 第39行：`"usb_audio_example.cc"` 已添加

## 🔧 开始硬件调试

### 步骤1：修改 main.cc

找到 `main/main.cc`，在 `app_main()` 函数中添加：

```cpp
#include "usb_audio_example.h"

extern "C" void app_main() {
    // 启动USB音频流测试
    usb_audio_example_start();
    
    // 按BOOT按钮（GPIO 9）可以切换音频流开关
    // 初始状态：关闭（可以看到日志）
    // 按一次按钮：开启音频流（日志将自动禁用，开始传输音频）
    // 再按一次：关闭音频流（日志恢复，显示统计信息）
}
```

### 步骤2：编译和烧录

```bash
# 确保在项目根目录
cd /Users/artrix/Documents/GitHub/xiaozhi-esp32

# 设置目标（如果还没设置）
idf.py set-target esp32c3

# 编译
idf.py build

# 烧录（会自动检测串口）
idf.py flash

# 查看日志
idf.py monitor
```

### 步骤3：预期的串口输出

连接后，你应该看到类似这样的日志：

```
I (xxx) XminiC3Board: Audio codec started: 24000 Hz, 1 channels
I (xxx) UsbAudioExample: === USB Audio Stream Example ===
I (xxx) UsbAudioExample: This example will stream microphone data via USB CDC
I (xxx) UsbAudioExample: Press BOOT button to toggle streaming on/off
I (xxx) UsbAudioExample: USB audio stream initialized
I (xxx) UsbAudioExample: Button handler set up on GPIO 9
I (xxx) UsbAudioExample: === Setup Complete ===
I (xxx) UsbAudioExample: Ready to stream audio data via USB
```

### 步骤4：启动音频流

**按一次BOOT按钮**（板子上的物理按钮，GPIO 9），你会看到：

```
I (xxx) UsbAudioExample: Starting USB audio stream...
I (xxx) UsbAudioExample: Logs will be disabled in 1 second...
W (xxx) UsbAudioStream: Warning: Disabling console log output to avoid interference
I (xxx) UsbAudioStream: Sample rate: 24000 Hz, Frame size: 240 samples (10.0 ms)
I (xxx) UsbAudioStream: UsbAudioStream initialized
```

**1秒后，日志会停止**（这是正常的，因为日志会干扰音频数据传输）。

### 步骤5：关闭 idf.py monitor

⚠️ **重要：** USB串口不能同时被monitor和音频流使用，必须关闭monitor！

按 `Ctrl+]` 退出 monitor

### 步骤6：运行Python接收脚本

在另一个终端窗口：

```bash
# 安装依赖（首次运行）
pip3 install pyserial numpy soundfile

# 启动接收脚本（替换为你的实际串口设备）
python3 scripts/usb_audio_receiver.py --port /dev/ttyACM0 --rate 24000 --realtime
```

**在macOS上查找串口：**
```bash
ls /dev/tty.* | grep -i usb
# 或
ls /dev/cu.* | grep -i usb
```

**预期输出：**
```
Opened serial port: /dev/ttyACM0 @ 2000000 baud

=== Real-time Mode ===
Receiving audio frames... Press Ctrl+C to stop
[1.0s] Frames: 100, Dropped: 0, Buffer: 100 frames
[2.0s] Frames: 200, Dropped: 0, Buffer: 200 frames
[3.0s] Frames: 300, Dropped: 0, Buffer: 300 frames
```

如果看到这个输出，**说明一切正常！** 🎉

### 步骤7：测试音频

对着麦克风说话或制造声音，Python脚本会持续接收音频帧。

按 `Ctrl+C` 停止，会显示统计信息：

```
=== Statistics ===
Frames received:  1000
Frames dropped:   0
Checksum errors:  0
Sync errors:      0
Bytes received:   326000
Success rate:     100.00%
```

### 步骤8：录制WAV文件（可选）

```bash
# 录制10秒音频
python3 scripts/usb_audio_receiver.py --port /dev/ttyACM0 --rate 24000 --duration 10 --output test.wav

# 播放测试
afplay test.wav  # macOS
# 或用任何音频播放器打开 test.wav
```

## 🐛 调试清单

### ✅ 编译阶段

```bash
idf.py build
```

**应该看到：**
```
Building ESP-IDF components for target esp32c3
...
[xxx/xxx] Linking CXX executable xiaozhi.elf
...
Project build complete.
```

**不应该有：**
- ❌ `undefined reference to` 错误
- ❌ `No such file or directory` 错误
- ❌ 任何关于 `usb_audio_stream` 的编译错误

如果编译失败，请检查：
1. 所有文件是否存在（见上面的文件列表）
2. CMakeLists.txt 是否正确更新
3. ESP-IDF 版本（推荐 v5.0+）

### ✅ 烧录阶段

```bash
idf.py flash
```

**应该看到：**
```
Detecting chip type... ESP32-C3
...
Hard resetting via RTS pin...
```

### ✅ 运行阶段

使用 `idf.py monitor` 查看启动日志：

**关键日志行：**
```
✓ "Audio codec started: 24000 Hz"
✓ "USB audio stream initialized"
✓ "Button handler set up on GPIO 9"
✓ "Ready to stream audio data via USB"
```

### ✅ 音频流阶段

**Python脚本运行后：**

**正常情况：**
```
✓ 成功打开串口
✓ 持续收到帧（Frames数字不断增加）
✓ Dropped = 0 或很小
✓ Checksum errors = 0
```

**异常情况及解决：**

| 问题 | 可能原因 | 解决方法 |
|------|---------|---------|
| `Could not open port` | 串口被占用或路径错误 | 1. 关闭 idf.py monitor<br>2. 检查串口路径 |
| `Sync errors` 很多 | 有旧的日志数据 | 等待几秒让缓冲区清空 |
| `Frames dropped` 很多 | USB传输不稳定 | 1. 换USB线<br>2. 增大帧大小<br>3. 降低采样率 |
| 完全收不到数据 | ESP32未启动音频流 | 按BOOT按钮启动 |

## 🔍 调试技巧

### 技巧1：逐步测试

```bash
# 1. 先只编译
idf.py build

# 2. 确认编译成功后再烧录
idf.py flash

# 3. 查看启动日志
idf.py monitor

# 4. 确认日志正常后，退出monitor（Ctrl+]）

# 5. 按BOOT按钮启动音频流

# 6. 运行Python脚本
python3 scripts/usb_audio_receiver.py --port /dev/ttyACM0 --rate 24000 --realtime
```

### 技巧2：查看详细日志

在启动音频流之前（还没按BOOT按钮时），ESP32会输出详细信息。

### 技巧3：重新启动

如果遇到问题：

```bash
# 1. 停止Python脚本（Ctrl+C）
# 2. 重启ESP32（按RST按钮）
# 3. 重新运行步骤3-6
```

### 技巧4：验证串口设备

```bash
# macOS
ls -l /dev/tty.* | grep -i usb
ls -l /dev/cu.* | grep -i usb

# 应该看到类似：
# /dev/tty.usbserial-xxx
# /dev/cu.usbserial-xxx
```

### 技巧5：测试麦克风

在Python接收时，对着麦克风说话。如果工作正常，`Frames` 计数应该持续增加。

## 📊 成功标志

如果以下所有项都满足，说明功能完全正常：

- [x] 编译无错误
- [x] 烧录成功
- [x] 启动日志显示初始化成功
- [x] 按BOOT按钮后日志停止（正常）
- [x] Python脚本成功打开串口
- [x] Python持续接收到音频帧
- [x] Dropped frames = 0 或很少
- [x] 可以录制WAV文件
- [x] WAV文件可以正常播放

## 🎯 下一步

成功运行后，你可以：

1. **修改采样率**：
   ```cpp
   usb_stream.Initialize(codec, 16000, 160);  // 16kHz
   ```

2. **自定义Python处理**：
   编辑 `scripts/usb_audio_receiver.py`，添加你的音频处理算法

3. **集成到现有项目**：
   参考 `USB_AUDIO_INTEGRATION_EXAMPLE.md`

## ⚠️ 注意事项

1. **USB串口独占**：音频流运行时，不能同时使用 `idf.py monitor`
2. **日志会自动禁用**：这是为了避免日志干扰音频数据
3. **BOOT按钮功能**：在这个示例中，BOOT按钮用于切换音频流开关
4. **串口设备路径**：macOS上可能是 `/dev/tty.usbserial-xxx` 或 `/dev/cu.usbserial-xxx`

## 📞 遇到问题？

如果遇到任何问题，请提供：
1. 编译输出（如果编译失败）
2. ESP32启动日志（`idf.py monitor` 的输出）
3. Python脚本的错误信息
4. 使用的串口设备路径

## 🎉 准备好了！

现在你可以开始在实际硬件上调试了！

**第一步：**
```bash
cd /Users/artrix/Documents/GitHub/xiaozhi-esp32
idf.py build
```

祝调试顺利！🚀

