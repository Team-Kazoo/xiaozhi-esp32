# USB音频流 - 集成示例

本文档提供了几种将USB音频流集成到现有项目中的方法。

## 方法1：最简单 - 使用示例函数（推荐入门）

### 修改 main/main.cc

在`app_main()`函数中添加一行调用：

```cpp
#include "usb_audio_example.h"

extern "C" void app_main() {
    // 启动USB音频流示例
    // 这会初始化音频编解码器并设置按钮控制
    usb_audio_example_start();
    
    // 按BOOT按钮（GPIO 9）切换音频流开关
    // 音频数据将通过USB串口发送到电脑
}
```

**优点：**
- 只需添加一行代码
- 自动处理所有初始化
- 提供按钮控制

**缺点：**
- 不能与其他功能并行运行
- 配置选项有限

## 方法2：基础集成 - 直接使用API

### 修改 main/main.cc

```cpp
#include "audio/usb_audio_stream.h"
#include "boards/common/board.h"
#include <esp_log.h>

static const char* TAG = "main";

extern "C" void app_main() {
    ESP_LOGI(TAG, "Starting USB Audio Stream");
    
    // 1. 获取板子实例
    Board& board = Board::GetInstance();
    
    // 2. 获取音频编解码器并启动
    AudioCodec* codec = board.GetAudioCodec();
    codec->Start();
    
    ESP_LOGI(TAG, "Audio codec initialized: %d Hz", codec->input_sample_rate());
    
    // 3. 创建USB音频流对象（静态，避免被销毁）
    static UsbAudioStream usb_stream;
    
    // 4. 初始化
    // 参数：编解码器, 采样率, 每帧采样点数
    // 24000 Hz, 240 samples = 10ms per frame
    if (!usb_stream.Initialize(codec, 24000, 240)) {
        ESP_LOGE(TAG, "Failed to initialize USB audio stream");
        return;
    }
    
    ESP_LOGI(TAG, "USB audio stream initialized");
    ESP_LOGI(TAG, "Logs will be disabled in 2 seconds...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 5. 启动音频流
    usb_stream.Start();
    
    // 音频数据现在持续通过USB发送
    // 运行Python脚本接收：
    // python3 scripts/usb_audio_receiver.py --port /dev/ttyACM0 --realtime
}
```

**优点：**
- 完全控制参数
- 代码简洁明了

**缺点：**
- 无按钮控制
- 需要手动处理初始化

## 方法3：高级集成 - 与现有功能并行

如果你想保留项目的AI功能，同时旁路输出音频数据：

### 修改 main/application.cc

在`Application::Initialize()`中添加：

```cpp
// application.cc

#include "audio/usb_audio_stream.h"

class Application {
private:
    UsbAudioStream* usb_audio_stream_;
    bool usb_streaming_enabled_;
    
public:
    void Initialize() {
        // ... 现有初始化代码 ...
        
        // 初始化USB音频流
        usb_audio_stream_ = new UsbAudioStream();
        AudioCodec* codec = Board::GetInstance().GetAudioCodec();
        usb_audio_stream_->Initialize(codec, 16000, 160);
        usb_streaming_enabled_ = false;
        
        ESP_LOGI(TAG, "USB audio stream available");
    }
    
    void EnableUsbStreaming(bool enable) {
        if (enable && !usb_streaming_enabled_) {
            usb_audio_stream_->Start();
            usb_streaming_enabled_ = true;
        } else if (!enable && usb_streaming_enabled_) {
            usb_audio_stream_->Stop();
            usb_streaming_enabled_ = false;
        }
    }
};
```

然后在需要的地方调用：

```cpp
// 开启USB音频流
Application::GetInstance().EnableUsbStreaming(true);

// AI功能继续正常工作
// USB同时输出音频数据副本
```

### 添加MCP工具控制（可选）

创建一个MCP工具让你可以通过AI命令控制：

```cpp
// usb_stream_mcp_tool.h
class UsbStreamMcpTool : public McpTool {
public:
    void Execute(const cJSON* args) override {
        bool enable = cJSON_GetObjectItem(args, "enable")->valueint;
        Application::GetInstance().EnableUsbStreaming(enable);
    }
};
```

**优点：**
- 不影响现有功能
- 可以动态开关
- AI和USB音频并行

**缺点：**
- 需要修改多个文件
- 稍微复杂

## 方法4：条件编译 - 可配置的集成

使用Kconfig选项在编译时选择：

### 创建 main/Kconfig.projbuild

```kconfig
menu "USB Audio Stream"
    config ENABLE_USB_AUDIO_STREAM
        bool "Enable USB Audio Stream"
        default n
        help
            Enable USB CDC audio streaming feature.
            Audio data will be sent via USB Serial/JTAG.
    
    config USB_AUDIO_SAMPLE_RATE
        int "USB Audio Sample Rate"
        depends on ENABLE_USB_AUDIO_STREAM
        default 24000
        help
            Sample rate for USB audio streaming (Hz).
    
    config USB_AUDIO_FRAME_SAMPLES
        int "USB Audio Frame Size (samples)"
        depends on ENABLE_USB_AUDIO_STREAM
        default 240
        help
            Number of samples per frame.
            240 samples @ 24kHz = 10ms per frame.
endmenu
```

### 修改 main/main.cc

```cpp
extern "C" void app_main() {
    // 现有的应用初始化
    Application& app = Application::GetInstance();
    app.Start();
    
#ifdef CONFIG_ENABLE_USB_AUDIO_STREAM
    // USB音频流功能（仅在启用时编译）
    static UsbAudioStream usb_stream;
    AudioCodec* codec = Board::GetInstance().GetAudioCodec();
    usb_stream.Initialize(
        codec,
        CONFIG_USB_AUDIO_SAMPLE_RATE,
        CONFIG_USB_AUDIO_FRAME_SAMPLES
    );
    usb_stream.Start();
    ESP_LOGI(TAG, "USB audio stream started");
#endif
}
```

### 配置

```bash
idf.py menuconfig
# 导航到 "USB Audio Stream" 菜单
# 启用并配置参数
```

**优点：**
- 可以通过menuconfig轻松开关
- 不启用时不占用资源
- 参数可配置

**缺点：**
- 需要重新编译才能改变设置

## 完整示例：独立程序

如果你想创建一个完全独立的USB音频流程序（不要任何AI功能）：

### 创建 main/main_usb_audio_only.cc

```cpp
/**
 * @file main_usb_audio_only.cc
 * @brief 纯USB音频流程序 - 不包含任何AI功能
 */

#include "audio/usb_audio_stream.h"
#include "boards/common/board.h"
#include "button.h"
#include <esp_log.h>

static const char* TAG = "USBAudioOnly";
static UsbAudioStream* g_stream = nullptr;
static bool g_running = false;

void toggle_stream() {
    if (g_running) {
        ESP_LOGI(TAG, "Stopping...");
        g_stream->Stop();
        g_running = false;
        
        // 重新启用日志
        esp_log_level_set("*", ESP_LOG_INFO);
        
        // 显示统计
        auto stats = g_stream->GetStatistics();
        ESP_LOGI(TAG, "Frames: %lu, Bytes: %lu, Errors: %lu/%lu",
                 stats.frames_sent, stats.bytes_sent,
                 stats.read_errors, stats.write_errors);
    } else {
        ESP_LOGI(TAG, "Starting in 1 second...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        g_stream->Start();
        g_running = true;
    }
}

extern "C" void app_main() {
    ESP_LOGI(TAG, "=== USB Audio Streaming Only ===");
    
    // 初始化板子
    Board& board = Board::GetInstance();
    
    // 初始化音频
    AudioCodec* codec = board.GetAudioCodec();
    codec->Start();
    ESP_LOGI(TAG, "Audio: %d Hz, %d channels",
             codec->input_sample_rate(),
             codec->input_channels());
    
    // 创建USB音频流
    g_stream = new UsbAudioStream();
    if (!g_stream->Initialize(codec, 24000, 240)) {
        ESP_LOGE(TAG, "Init failed!");
        return;
    }
    
    // 设置按钮（GPIO 9 = BOOT button on Xmini C3 V3）
    static Button button(GPIO_NUM_9);
    button.OnClick(toggle_stream);
    
    ESP_LOGI(TAG, "Press BOOT button to toggle streaming");
    ESP_LOGI(TAG, "Ready!");
    
    // 保持程序运行
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

### 替换main.cc

```bash
# 备份原来的main.cc
mv main/main.cc main/main.cc.backup

# 使用USB音频版本
mv main/main_usb_audio_only.cc main/main.cc

# 编译
idf.py build flash
```

## 参数调整指南

### 采样率选择

```cpp
// 语音识别优化（节省带宽）
usb_stream.Initialize(codec, 16000, 160);  // 10ms/帧

// 板子原生采样率（无重采样开销）
usb_stream.Initialize(codec, 24000, 240);  // 10ms/帧

// 高保真音频
usb_stream.Initialize(codec, 48000, 480);  // 10ms/帧
```

### 帧大小调整

```cpp
// 低延迟模式（5ms/帧）
usb_stream.Initialize(codec, 24000, 120);

// 标准模式（10ms/帧，推荐）
usb_stream.Initialize(codec, 24000, 240);

// 高带宽模式（20ms/帧，更稳定）
usb_stream.Initialize(codec, 24000, 480);
```

## 调试技巧

### 1. 保留日志（使用独立UART）

```cpp
// 在Initialize前配置UART日志
esp_log_set_vprintf(uart_log_vprintf);  // 自定义日志函数
// 然后USB串口就只有音频数据
```

### 2. 添加LED指示

```cpp
Led* led = Board::GetInstance().GetLed();

usb_stream.Start();
led->SetRgb(0, 255, 0);  // 绿色 = 正在传输

usb_stream.Stop();
led->SetRgb(255, 0, 0);  // 红色 = 已停止
```

### 3. 周期性统计输出

```cpp
// 每10秒输出一次统计（临时启用日志）
void stats_task(void* arg) {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        
        g_usb_stream->Stop();  // 停止以便输出日志
        esp_log_level_set("*", ESP_LOG_INFO);
        
        auto stats = g_usb_stream->GetStatistics();
        ESP_LOGI(TAG, "Stats: %lu frames", stats.frames_sent);
        
        vTaskDelay(pdMS_TO_TICKS(500));
        g_usb_stream->Start();  // 继续
    }
}
```

## 常见问题

### Q: 如何同时使用idf.py monitor？

A: 不能同时使用。monitor和USB音频流共用同一个串口。选择：
- 调试时：使用monitor，禁用USB音频流
- 运行时：使用USB音频流，不用monitor

### Q: 可以用JTAG调试吗？

A: 可以，使用外部JTAG调试器（如ESP-Prog），不影响USB串口。

### Q: 如何减少CPU占用？

A: 
1. 增大帧大小（减少处理频率）
2. 使用板子原生采样率（避免重采样）
3. 禁用不需要的功能

### Q: 音频有噪音？

A: 检查：
1. USB线质量
2. 电源质量
3. 麦克风连接
4. 音频增益设置

## 下一步

选择一个方法，开始集成吧！

建议顺序：
1. 先用**方法1**快速验证功能
2. 然后用**方法2**理解细节
3. 最后根据需求选择**方法3**或**方法4**

祝你成功！🎤✨

