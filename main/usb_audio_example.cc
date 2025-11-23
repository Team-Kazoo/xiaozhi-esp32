/**
 * @file usb_audio_example.cc
 * @brief USB音频流传输示例
 * 
 * 这个示例展示如何使用UsbAudioStream类将麦克风数据
 * 通过USB串口传输到电脑端。
 * 
 * 使用方法：
 * 1. 在main.cc中调用 usb_audio_example_start()
 * 2. 在电脑端运行Python接收脚本
 * 3. 按BOOT按钮切换音频流的开关状态
 */

#include "audio/usb_audio_stream.h"
#include "audio/audio_codec.h"
#include "boards/common/board.h"
#include "button.h"
#include <esp_log.h>

#define TAG "UsbAudioExample"

static UsbAudioStream* g_usb_stream = nullptr;
static Button* g_boot_button = nullptr;
static bool g_is_streaming = false;

/**
 * @brief 按钮回调：切换音频流状态
 */
static void toggle_streaming() {
    if (g_usb_stream == nullptr) {
        return;
    }
    
    if (g_is_streaming) {
        g_usb_stream->Stop();
        g_is_streaming = false;
        esp_log_level_set("*", ESP_LOG_INFO);
        vTaskDelay(pdMS_TO_TICKS(100));
    } else {
        vTaskDelay(pdMS_TO_TICKS(500));
        g_usb_stream->Start();
        g_is_streaming = true;
    }
}

// C linkage for functions called from C code
extern "C" {

/**
 * @brief 初始化并启动USB音频流示例
 */
void usb_audio_example_start() {
    ESP_LOGI(TAG, "USB Audio Stream starting...");
    
    // 获取音频编解码器
    Board& board = Board::GetInstance();
    AudioCodec* codec = board.GetAudioCodec();
    
    if (codec == nullptr) {
        return;
    }
    
    // 启动音频编解码器
    codec->Start();
    
    // 创建USB音频流（低延迟模式：2.5ms帧，48kHz采样率）
    g_usb_stream = new UsbAudioStream();
    
    if (!g_usb_stream->Initialize(codec, 48000, 120)) {
        delete g_usb_stream;
        g_usb_stream = nullptr;
        return;
    }
    
    // 设置按钮回调
#ifdef BOOT_BUTTON_GPIO
    g_boot_button = new Button(BOOT_BUTTON_GPIO);
    g_boot_button->OnClick(toggle_streaming);
#else
    vTaskDelay(pdMS_TO_TICKS(1000));
    toggle_streaming();
#endif
    
    ESP_LOGI(TAG, "Ready - Press BOOT button to toggle");
}

/**
 * @brief 停止USB音频流示例
 */
void usb_audio_example_stop() {
    if (g_usb_stream != nullptr) {
        g_usb_stream->Stop();
        delete g_usb_stream;
        g_usb_stream = nullptr;
    }
    
    if (g_boot_button != nullptr) {
        delete g_boot_button;
        g_boot_button = nullptr;
    }
    
    g_is_streaming = false;
}

} // extern "C"

