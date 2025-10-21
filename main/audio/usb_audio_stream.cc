#include "usb_audio_stream.h"
#include <esp_log.h>
#include <driver/usb_serial_jtag.h>
#include <cstring>

#define TAG "UsbAudioStream"

UsbAudioStream::UsbAudioStream() 
    : codec_(nullptr)
    , sample_rate_(16000)
    , frame_samples_(160)
    , running_(false)
    , seq_num_(0)
    , task_handle_(nullptr)
    , need_resample_(false) {
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
        sample_rate_ = codec_->input_sample_rate();
    }
    
    // 配置USB Serial/JTAG - 低延迟模式
    usb_serial_jtag_driver_config_t usb_serial_config = {
        .tx_buffer_size = 4096,  // 增大缓冲以支持高频传输
        .rx_buffer_size = 2048,
    };
    
    esp_err_t ret = usb_serial_jtag_driver_install(&usb_serial_config);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return false;
    }
    
    // 禁用日志输出避免干扰音频数据
    vTaskDelay(pdMS_TO_TICKS(100));  // 让日志输出完成
    esp_log_level_set("*", ESP_LOG_NONE);
    
    return true;
}

void UsbAudioStream::Start() {
    if (running_) {
        return;
    }
    
    running_ = true;
    seq_num_ = 0;
    memset(&stats_, 0, sizeof(stats_));
    
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
    
    while (stream->running_) {
        stream->ProcessFrame();
    }
    
    vTaskDelete(nullptr);
}

void UsbAudioStream::ProcessFrame() {
    // 分配缓冲区
    std::vector<int16_t> audio_data(frame_samples_ * codec_->input_channels());
    
    // 读取音频数据
    if (!codec_->InputData(audio_data)) {
        stats_.read_errors++;
        vTaskDelay(pdMS_TO_TICKS(1));
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
    } else {
        stats_.write_errors++;
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
    
    // 发送数据
    int written = usb_serial_jtag_write_bytes(buffer.data(), total_size, pdMS_TO_TICKS(10));
    
    if (written == total_size) {
        stats_.bytes_sent += written;
        return true;
    }
    
    return false;
}

uint16_t UsbAudioStream::CalculateChecksum(const uint8_t* data, size_t length) {
    uint16_t sum = 0;
    for (size_t i = 0; i < length; i++) {
        sum += data[i];
    }
    return sum;
}

