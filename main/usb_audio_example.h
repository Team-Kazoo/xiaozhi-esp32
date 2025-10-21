/**
 * @file usb_audio_example.h
 * @brief USB音频流传输示例头文件
 */

#ifndef USB_AUDIO_EXAMPLE_H
#define USB_AUDIO_EXAMPLE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动USB音频流示例
 * 
 * 这个函数会：
 * 1. 初始化音频编解码器
 * 2. 创建并配置USB音频流
 * 3. 设置按钮控制（如果可用）
 */
void usb_audio_example_start();

/**
 * @brief 停止USB音频流示例
 */
void usb_audio_example_stop();

#ifdef __cplusplus
}
#endif

#endif // USB_AUDIO_EXAMPLE_H

