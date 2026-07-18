#include <iostream>
#include <string>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
}

/**
 * FFmpeg 音频解码测试程序
 * 
 * FFmpeg 工作流程概述：
 * 
 * 1. AVFormatContext (封装格式上下文)
 *    - 负责打开媒体文件，读取封装格式信息
 *    - 例如：MP4 文件、AVI 文件、WAV 文件等
 *    - 包含文件时长、Streams（流）等信息
 * 
 * 2. AVStream (流)
 *    - 媒体文件中的音频流、视频流、字幕流等
 *    - 每个流有一个 AVCodecParameters 描述编码参数
 * 
 * 3. AVCodec (编解码器)
 *    - 编解码器的元信息，如名称、ID 等
 *    - 例如：libmp3lame (MP3解码)、aac (AAC解码)
 * 
 * 4. AVCodecContext (编解码器上下文)
 *    - 编解码器的运行时上下文
 *    - 实际进行编解码操作的核心对象
 *    - 包含采样率、通道数、位宽等详细信息
 * 
 * 5. AVPacket (数据包)
 *    - 编码后的数据单元（压缩数据）
 *    - 可能包含一个或多个帧
 *    - 包含时间戳(PTS)、DTS 等信息
 * 
 * 6. AVFrame (帧)
 *    - 解码后的原始数据（原始音频/视频）
 *    - 音频：包含 PCM 样本数据
 *    - 视频：包含像素数据
 */

int main(int argc, char* argv[]) {
    std::cout << "=== Digital Human SDK: FFmpeg Audio Test ===" << std::endl;

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <audio_file_path>" << std::endl;
        return -1;
    }

    const char* filename = argv[1];
    
    // ========================================================================
    // 步骤 1: 打开输入文件，建立封装格式上下文
    // ========================================================================
    // avformat_open_input:
    //   - 根据文件名自动检测文件格式（MP4, WAV, MP3, FLAC 等）
    //   - 分配 AVFormatContext 并打开文件
    //   - 读取文件头信息，但尚未解析流信息
    //
    // 参数说明:
    //   - &format_ctx: 输出参数，返回创建的 AVFormatContext
    //   - filename: 要打开的文件路径
    //   - 第二个参数: 指定输入格式(AVInputFormat)，通常设为 nullptr 自动检测
    //   - 第三个参数: 字典类型的选项，通常设为 nullptr
    AVFormatContext* format_ctx = nullptr;
    if (avformat_open_input(&format_ctx, filename, nullptr, nullptr) < 0) {
        std::cerr << "[Error] Could not open file: " << filename << std::endl;
        return -1;
    }
    
    // format_ctx->duration: 文件总时长，单位是微秒
    // AV_TIME_BASE = 1000000，所以 duration / AV_TIME_BASE = 秒数
    std::cout << "[Success] File opened. Duration: " << format_ctx->duration / AV_TIME_BASE << " seconds" << std::endl;

    // ========================================================================
    // 步骤 2: 读取流信息
    // ========================================================================
    // avformat_find_stream_info:
    //   - 读取足够多的包来解析所有流的信息
    //   - 填充每个 AVStream 的 codecpar (编码参数)
    //   - 这是一个耗时操作，对于网络流尤其明显
    if (avformat_find_stream_info(format_ctx, nullptr) < 0) {
        std::cerr << "[Error] Could not find stream info" << std::endl;
        avformat_close_input(&format_ctx);
        return -1;
    }

    // ========================================================================
    // 步骤 3: 查找音频流
    // ========================================================================
    // 遍历所有流，找到类型为 AVMEDIA_TYPE_AUDIO 的流
    // 一个媒体文件可能包含多个流（视频流、音频流、字幕流等）
    //
    // format_ctx->nb_streams: 流的数量
    // format_ctx->streams[i]: 第 i 个流
    // streams[i]->codecpar->codec_type: 流的数据类型 (音频/视频/字幕)
    int audio_stream_idx = -1;
    for (unsigned int i = 0; i < format_ctx->nb_streams; i++) {
        if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_idx = i;
            break;
        }
    }

    if (audio_stream_idx == -1) {
        std::cerr << "[Error] No audio stream found!" << std::endl;
        avformat_close_input(&format_ctx);
        return -1;
    }
    std::cout << "[Success] Found audio stream at index: " << audio_stream_idx << std::endl;

    // ========================================================================
    // 步骤 4: 初始化解码器
    // ========================================================================
    
    // 4.1 获取音频流的编码参数
    // codecpar 包含编码器的类型(ID)、比特率、采样率、通道数等信息
    AVCodecParameters* codec_params = format_ctx->streams[audio_stream_idx]->codecpar;
    
    // 4.2 根据编码器 ID 查找对应的解码器
    // avcodec_find_decoder:
    //   - 根据 codec_id 查找对应的解码器
    //   - codec_id 可能是 AV_CODEC_ID_AAC, AV_CODEC_ID_MP3, AV_CODEC_ID_FLAC 等
    //   - 如果找不到解码器返回 nullptr
    const AVCodec* codec = avcodec_find_decoder(codec_params->codec_id);
    if (!codec) {
        std::cerr << "[Error] Decoder not found!" << std::endl;
        return -1;
    }

    // 4.3 分配解码器上下文
    // AVCodecContext 是进行编解码操作的核心对象
    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
    
    // 4.4 将流的编码参数复制到解码器上下文
    // 这一步非常重要！它将流中存储的编码参数传递给解码器
    avcodec_parameters_to_context(codec_ctx, codec_params);

    // 4.5 打开解码器
    // 分配解码器所需的内部资源
    // 第三个参数是 AVDictionary，可以传入解码选项
    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        std::cerr << "[Error] Could not open codec" << std::endl;
        return -1;
    }
    
    std::cout << "[Success] Decoder initialized: " << codec->name << std::endl;
    std::cout << "   -> Sample Rate: " << codec_ctx->sample_rate << " Hz" << std::endl;
    std::cout << "   -> Channels: " << codec_ctx->channels << std::endl;

    // ========================================================================
    // 步骤 5: 解码循环 - 读取数据包并解码
    // ========================================================================
    // 
    // FFmpeg 解码流程采用 "发送-接收" 模式:
    //   1. avcodec_send_packet(): 发送压缩数据 (AVPacket) 给解码器
    //   2. avcodec_receive_frame(): 从解码器接收原始数据 (AVFrame)
    // 
    // 这种模式的优势:
    //   - 解码器可以缓冲数据，支持 B 帧等场景
    //   - 可以一次发送多个包，然后批量接收
    //   - 支持零拷贝等优化
    
    // 5.1 分配 AVPacket 和 AVFrame
    // AVPacket: 存储压缩后的编码数据
    // AVFrame: 存储解码后的原始数据
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    
    int frame_count = 0;
    int max_frames_to_decode = 10;

    std::cout << "\n--- Start Decoding (First 10 frames) ---" << std::endl;

    // 5.2 循环读取文件中的数据包
    // av_read_frame:
    //   - 从文件中读取一个 AVPacket
    //   - 返回 0 表示成功，返回负值表示错误或文件结束
    while (av_read_frame(format_ctx, packet) >= 0) {
        // 只处理音频流的数据包
        if (packet->stream_index == audio_stream_idx) {
            
            // 5.3 发送数据包到解码器
            // 一个 AVPacket 可能包含多个音频帧（尤其是 MP3）
            // 发送成功返回 0，失败返回负值
            int ret = avcodec_send_packet(codec_ctx, packet);
            if (ret < 0) {
                std::cerr << "Error sending packet" << std::endl;
                break;
            }

            // 5.4 循环接收解码后的帧
            // 可能的返回值:
            //   - AVERROR(EAGAIN): 解码器需要更多数据
            //   - AVERROR_EOF: 解码器已处理完所有数据
            //   - 负值: 解码错误
            //   - >= 0: 成功解码出一帧
            while (ret >= 0) {
                ret = avcodec_receive_frame(codec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    std::cerr << "Error decoding frame" << std::endl;
                    break;
                }

                frame_count++;
                if (frame_count <= max_frames_to_decode) {
                    // frame->nb_samples: 这一帧包含的样本数
                    // frame->pts: 显示时间戳 (Presentation Time Stamp)
                    std::cout << "Frame " << frame_count 
                              << ": samples=" << frame->nb_samples 
                              << ", pts=" << frame->pts << std::endl;
                }
            }
        }
        
        // 5.5 释放数据包引用
        // 重要：每次使用完 packet 后必须调用 av_packet_unref
        // 这会减少 packet 的引用计数，如果为 0 则释放内存
        av_packet_unref(packet);
        
        if (frame_count >= max_frames_to_decode) break;
    }

    // ========================================================================
    // 步骤 6: 释放资源
    // ========================================================================
    // 遵循 "谁分配，谁释放" 的原则
    // 释放顺序：Frame -> Packet -> CodecContext -> FormatContext
    
    // 释放 AVFrame（同时会释放其内部的缓冲区）
    av_frame_free(&frame);
    
    // 释放 AVPacket
    av_packet_free(&packet);
    
    // 释放解码器上下文
    avcodec_free_context(&codec_ctx);
    
    // 关闭输入文件，释放 AVFormatContext
    avformat_close_input(&format_ctx);

    std::cout << "\n=== Test Finished Successfully ===" << std::endl;
    return 0;
}