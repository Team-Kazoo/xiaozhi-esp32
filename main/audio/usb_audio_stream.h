#ifndef USB_AUDIO_STREAM_H
#define USB_AUDIO_STREAM_H

#include "audio_codec.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <vector>
#include <cstdint>

/**
 * @brief USB Audio Stream - 通过USB CDC串口传输音频数据
 */
class UsbAudioStream {
public:
    /**
     * @brief 音频帧结构
     */
    struct AudioFrame {
        uint16_t magic;      // 魔数：0xAA55
        uint16_t seq_num;    // 序列号
        uint16_t length;     // 数据长度（字节数）
    } __attribute__((packed));

    static constexpr uint16_t FRAME_MAGIC = 0xAA55;
    static constexpr int MAX_FRAME_SIZE = 1024;
    
    UsbAudioStream();
    ~UsbAudioStream();

    /**
     * @brief 初始化USB音频流
     */
    bool Initialize(AudioCodec* codec, int sample_rate = 16000, int frame_samples = 160);

    /**
     * @brief 开始传输音频流
     */
    void Start();

    /**
     * @brief 停止传输音频流
     */
    void Stop();

    /**
     * @brief 检查是否正在运行
     */
    bool IsRunning() const { return running_; }

    /**
     * @brief 获取统计信息
     */
    struct Statistics {
        uint32_t frames_sent;
        uint32_t bytes_sent;
        uint32_t read_errors;
        uint32_t write_errors;
        uint32_t frames_dropped;
    };
    
    Statistics GetStatistics() const { return stats_; }

private:
    AudioCodec* codec_;
    int sample_rate_;
    int frame_samples_;
    bool running_;
    uint16_t seq_num_;
    
    TaskHandle_t task_handle_;
    Statistics stats_;
    
    bool need_resample_;
    std::vector<int16_t> resample_buffer_;
    
    // 连接状态和重连相关
    bool is_connected_;
    uint32_t consecutive_failures_;
    uint32_t reconnect_attempts_;
    static constexpr uint32_t MAX_CONSECUTIVE_FAILURES = 3;   // 连续失败次数阈值（降低以更快检测断开）
    static constexpr uint32_t RECONNECT_INTERVAL_MS = 500;    // 重连间隔（毫秒，降低以更快重连）
    static constexpr uint32_t MAX_RECONNECT_ATTEMPTS = 20;    // 最大重连尝试次数（避免无限重连）
    
    // 时间流控相关
    TickType_t last_frame_time_;                              // 上一帧的时间戳
    TickType_t frame_interval_ticks_;                        // 每帧的时间间隔（tick数）
    uint32_t frames_dropped_;                                // 丢帧计数

    static void AudioStreamTask(void* arg);
    void ProcessFrame();
    static uint16_t CalculateChecksum(const uint8_t* data, size_t length);
    bool SendFrame(const int16_t* data, int samples);
    bool Reconnect();
};

#endif // USB_AUDIO_STREAM_H

