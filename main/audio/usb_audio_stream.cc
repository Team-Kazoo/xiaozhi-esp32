#include "usb_audio_stream.h"
#include <esp_log.h>
#include <driver/usb_serial_jtag.h>
#include <cstring>
#include <inttypes.h>

#define TAG "UsbAudioStream"

UsbAudioStream::UsbAudioStream() 
    : codec_(nullptr)
    , sample_rate_(16000)
    , frame_samples_(160)
    , running_(false)
    , seq_num_(0)
    , task_handle_(nullptr)
    , need_resample_(false)
    , is_connected_(false)
    , consecutive_failures_(0)
    , reconnect_attempts_(0)
    , last_frame_time_(0)
    , frame_interval_ticks_(0)
    , frames_dropped_(0) {
    memset(&stats_, 0, sizeof(stats_));
}

UsbAudioStream::~UsbAudioStream() {
    Stop();
}

bool UsbAudioStream::Initialize(AudioCodec* codec, int sample_rate, int frame_samples) {
    if (codec == nullptr || frame_samples > MAX_FRAME_SIZE) {
        return false;
    }
    
    codec_ = codec;
    sample_rate_ = sample_rate;
    frame_samples_ = frame_samples;
    
    // 检查是否需要重采样（当前未实现，使用原生采样率）
    need_resample_ = (codec_->input_sample_rate() != sample_rate_);
    if (need_resample_) {
        // 如果编解码器采样率与请求的不匹配，使用编解码器的采样率
        // 并相应调整帧大小以保持相同的帧时长
        int actual_sample_rate = codec_->input_sample_rate();
        int original_frame_samples = frame_samples_;
        // 计算帧时长（毫秒）
        int frame_duration_ms = (frame_samples_ * 1000) / sample_rate_;
        // 根据实际采样率重新计算帧大小
        frame_samples_ = (actual_sample_rate * frame_duration_ms) / 1000;
        sample_rate_ = actual_sample_rate;
        
        // 临时启用日志以输出警告
        int requested_rate = sample_rate;  // 保存原始请求的采样率
        esp_log_level_set(TAG, ESP_LOG_WARN);
        ESP_LOGW(TAG, "Codec sample rate (%d Hz) != requested (%d Hz), using %d Hz with %d samples/frame (was %d)",
                 actual_sample_rate, requested_rate, sample_rate_, frame_samples_, original_frame_samples);
        esp_log_level_set(TAG, ESP_LOG_NONE);
    }
    
    // 配置USB Serial/JTAG - 低延迟模式
    // 减小缓冲区大小以减少延迟累积，但保持足够大小以避免丢帧
    usb_serial_jtag_driver_config_t usb_serial_config = {
        .tx_buffer_size = 2048,  // 减小缓冲以减少延迟累积（原4096）
        .rx_buffer_size = 1024,  // 减小接收缓冲
    };
    
    esp_err_t ret = usb_serial_jtag_driver_install(&usb_serial_config);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return false;
    }
    
    // 禁用全局日志输出避免干扰音频数据，但保留本模块日志以便调试
    vTaskDelay(pdMS_TO_TICKS(100));  // 让日志输出完成
    esp_log_level_set("*", ESP_LOG_NONE);
    esp_log_level_set(TAG, ESP_LOG_INFO);
    
    // 初始化连接状态
    is_connected_ = true;
    consecutive_failures_ = 0;
    reconnect_attempts_ = 0;
    
    // 计算帧间隔时间（毫秒），并转换为tick数
    // 帧时长 = frame_samples / sample_rate (秒)
    int frame_duration_ms = (frame_samples_ * 1000) / sample_rate_;
    frame_interval_ticks_ = pdMS_TO_TICKS(frame_duration_ms);
    last_frame_time_ = xTaskGetTickCount();
    
    return true;
}

void UsbAudioStream::Start() {
    if (running_) {
        return;
    }
    
    running_ = true;
    seq_num_ = 0;
    memset(&stats_, 0, sizeof(stats_));
    is_connected_ = true;
    consecutive_failures_ = 0;
    reconnect_attempts_ = 0;
    frames_dropped_ = 0;
    last_frame_time_ = xTaskGetTickCount();
    
    // 启用音频输入
    if (!codec_->input_enabled()) {
        codec_->EnableInput(true);
    }
    
    // 创建音频流任务 - 高优先级以减少延迟
    xTaskCreate(AudioStreamTask, "usb_audio", 4096, this, 10, &task_handle_);
}

void UsbAudioStream::Stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    
    if (task_handle_ != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(100));  // 等待任务结束
        task_handle_ = nullptr;
    }
}

void UsbAudioStream::AudioStreamTask(void* arg) {
    UsbAudioStream* stream = static_cast<UsbAudioStream*>(arg);
    
    // 初始化时间戳
    stream->last_frame_time_ = xTaskGetTickCount();
    
    while (stream->running_) {
        TickType_t current_time = xTaskGetTickCount();
        TickType_t elapsed = current_time - stream->last_frame_time_;
        
        // 如果延迟过大（超过3倍帧间隔），说明处理严重滞后，跳过该帧
        // 这样可以防止延迟无限累积
        if (elapsed > stream->frame_interval_ticks_ * 3) {
            stream->frames_dropped_++;
            stream->stats_.frames_dropped++;
            // 更新时间为当前时间，避免连续丢帧
            stream->last_frame_time_ = current_time;
            // 短暂延迟后继续，避免CPU占用过高
            vTaskDelay(1);
            continue;
        }
        
        // 立即处理帧，不等待 - 这样可以最小化延迟
        // InputData会从I2S DMA缓冲区读取，如果数据还没准备好会立即返回false
        stream->ProcessFrame();
        
        // 更新时间戳
        stream->last_frame_time_ = xTaskGetTickCount();
        
        // 如果处理太快（小于帧间隔的50%），短暂延迟以避免CPU占用过高
        // 但保持很小的延迟以维持最低延迟
        elapsed = xTaskGetTickCount() - current_time;
        if (elapsed < stream->frame_interval_ticks_ / 2) {
            TickType_t wait_time = (stream->frame_interval_ticks_ / 2) - elapsed;
            if (wait_time > 0 && wait_time < stream->frame_interval_ticks_) {
                vTaskDelay(wait_time);
            }
        }
    }
    
    vTaskDelete(nullptr);
}

void UsbAudioStream::ProcessFrame() {
    // 检查连接状态
    if (!is_connected_) {
        // 限制重连尝试次数，避免无限重连
        if (reconnect_attempts_ >= MAX_RECONNECT_ATTEMPTS) {
            // 达到最大重连次数，等待更长时间后重置计数器
            vTaskDelay(pdMS_TO_TICKS(RECONNECT_INTERVAL_MS * 5));
            reconnect_attempts_ = 0;
        }
        
        // 尝试重连
        reconnect_attempts_++;
        if (Reconnect()) {
            // 重连后，先尝试发送一个测试帧来验证连接
            // 这里我们直接继续处理，让后续的 SendFrame 来验证
            is_connected_ = true;
            consecutive_failures_ = 0;
            reconnect_attempts_ = 0;  // 重置重连计数
        } else {
            // 重连失败，等待后重试
            vTaskDelay(pdMS_TO_TICKS(RECONNECT_INTERVAL_MS));
            return;
        }
    }
    
    // 分配缓冲区
    std::vector<int16_t> audio_data(frame_samples_ * codec_->input_channels());
    
    // 读取音频数据
    // InputData从I2S DMA缓冲区读取，如果缓冲区数据不足会返回false
    // 这种情况下不延迟，立即返回，让下一轮循环继续尝试
    if (!codec_->InputData(audio_data)) {
        stats_.read_errors++;
        // 不延迟，立即返回以保持低延迟
        return;
    }
    
    // 如果是双声道，只取左声道
    std::vector<int16_t> mono_data;
    if (codec_->input_channels() == 2) {
        mono_data.resize(frame_samples_);
        for (int i = 0; i < frame_samples_; i++) {
            mono_data[i] = audio_data[i * 2];  // 左声道
        }
    } else {
        mono_data = std::move(audio_data);
    }
    
    // 发送帧
    if (SendFrame(mono_data.data(), frame_samples_)) {
        stats_.frames_sent++;
        consecutive_failures_ = 0;  // 重置失败计数
        // 发送成功，确保连接状态为已连接
        is_connected_ = true;
        reconnect_attempts_ = 0;
        if ((stats_.frames_sent % 200) == 0) {
            ESP_LOGI(TAG, "frames=%" PRIu32 " dropped=%" PRIu32 " read_err=%" PRIu32 " write_err=%" PRIu32,
                     stats_.frames_sent, stats_.frames_dropped,
                     stats_.read_errors, stats_.write_errors);
        }
    } else {
        stats_.write_errors++;
        consecutive_failures_++;
        
        // 如果连续失败次数超过阈值，标记为断开连接
        if (consecutive_failures_ >= MAX_CONSECUTIVE_FAILURES) {
            is_connected_ = false;
            reconnect_attempts_ = 0;  // 重置重连计数，准备开始新的重连周期
        }
    }
}

bool UsbAudioStream::SendFrame(const int16_t* data, int samples) {
    // 计算总大小
    size_t audio_bytes = samples * sizeof(int16_t);
    size_t total_size = sizeof(AudioFrame) + audio_bytes + sizeof(uint16_t);
    
    // 分配发送缓冲区
    std::vector<uint8_t> buffer(total_size);
    uint8_t* ptr = buffer.data();
    
    // 填充帧头
    AudioFrame* frame = reinterpret_cast<AudioFrame*>(ptr);
    frame->magic = FRAME_MAGIC;
    frame->seq_num = seq_num_++;
    frame->length = audio_bytes;
    ptr += sizeof(AudioFrame);
    
    // 填充音频数据
    memcpy(ptr, data, audio_bytes);
    ptr += audio_bytes;
    
    // 计算并填充校验和
    uint16_t checksum = CalculateChecksum(buffer.data(), total_size - sizeof(uint16_t));
    memcpy(ptr, &checksum, sizeof(uint16_t));
    
    // 发送数据（使用非阻塞模式，超时时间设为帧间隔的1/4）
    // 如果USB缓冲区满，快速返回失败，避免阻塞导致延迟累积
    TickType_t timeout_ticks = frame_interval_ticks_ / 4;
    if (timeout_ticks == 0) {
        timeout_ticks = pdMS_TO_TICKS(1);  // 至少1ms
    }
    // 对于48kHz@120采样点，帧间隔约2.5ms，超时约0.6ms
    
    int written = usb_serial_jtag_write_bytes(buffer.data(), total_size, timeout_ticks);
    
    // 检查写入结果：如果写入字节数为0或负数，说明连接断开或缓冲区满
    if (written <= 0) {
        return false;
    }
    
    // 如果写入的字节数小于预期，也认为失败（可能是缓冲区满）
    if (written < static_cast<int>(total_size)) {
        return false;
    }
    
    stats_.bytes_sent += written;
    return true;
}

uint16_t UsbAudioStream::CalculateChecksum(const uint8_t* data, size_t length) {
    uint16_t sum = 0;
    for (size_t i = 0; i < length; i++) {
        sum += data[i];
    }
    return sum;
}

bool UsbAudioStream::Reconnect() {
    // 先尝试卸载驱动（如果已安装）
    usb_serial_jtag_driver_uninstall();
    vTaskDelay(pdMS_TO_TICKS(50));  // 减少等待时间
    
    // 重新配置并安装驱动
    usb_serial_jtag_driver_config_t usb_serial_config = {
        .tx_buffer_size = 4096,
        .rx_buffer_size = 2048,
    };
    
    esp_err_t ret = usb_serial_jtag_driver_install(&usb_serial_config);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return false;
    }
    
    // 等待驱动就绪（减少等待时间）
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // 重置序列号，表示新的连接
    seq_num_ = 0;
    
    // 返回true，让后续的正常发送来验证连接是否真正恢复
    // 因为主机端可能还没完全准备好，但驱动已经安装成功
    return true;
}

