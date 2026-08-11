/**
 * @file audio_process.cpp
 * @brief 音频处理模块实现 - 负责从 PCM 流中提取 Mel 特征并组装成 Wav2Lip 所需的格式
 *
 * 核心功能：
 * 1. 接收原始 PCM 音频数据流
 * 2. 使用滑动窗口进行分帧（50ms 窗口，12.5ms 步长）
 * 3. 对每帧进行预加重和加窗处理
 * 4. 提取 Mel 频谱特征（80 维）
 * 5. 按 25FPS 视频帧率要求，将 16 个连续 Mel 帧组成一个 chunk（80×16=1280 维）
 * 6. 通过回调函数将特征输出给推理引擎
 */

#include <iostream>
#include <cmath>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <deque>
#include <functional>
#include <cstring>
#include <algorithm>

#include "audio/audio_process.h"
#include "audio/audio_preprocessor.h"
#include "audio/audio_mel_feature_extract.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace DigitalHuman {
namespace Audio {

/**
 * @brief 原始音频数据块结构体
 *
 * 用于在输入队列中传输音频数据及其时间戳信息
 */
struct RawAudioBlock {
    std::vector<float> data;   // PCM 浮点采样数据（通常归一化到 [-1, 1]）
    double start_pts;          // 该音频块起始点的展示时间戳（毫秒）
};

/**
 * @brief AudioProcessor 的私有实现类（PImpl 模式）
 *
 * 隐藏所有实现细节，包括：
 * - 音频处理参数配置
 * - 内部缓冲区和队列
 * - 线程同步原语
 * - 特征提取组件
 */
struct AudioProcessor::Impl {
    /* ==================== 配置参数 ==================== */
    AudioProcessor::FeatureCallback output_cb_;   // 特征输出回调函数
    int sample_rate_;                             // 音频采样率（默认 16000 Hz）
    int frame_size_;                              // 帧长 = sample_rate * 50ms（默认 800 样本）
    int stride_size_;                             // 步长 = sample_rate * 12.5ms（默认 200 样本）

    /* ==================== 处理组件 ==================== */
    std::unique_ptr<AudioPreprocessor> preprocessor_;      // 预处理器（预加重等）
    std::unique_ptr<MelFeatureExtractor> mel_extractor_;   // Mel 特征提取器
    std::vector<float> hamming_window_;                     // Hamming 窗函数系数表

    /* ==================== 线程控制 ==================== */
    std::thread worker_thread_;                  // 后台工作线程
    std::atomic<bool> is_running_{false};        // 线程运行标志（控制生命周期）
    std::atomic<bool> is_processing_{false};     // 正在处理数据标志（用于 isDrained 判断）
    mutable std::mutex mtx_;                     // 互斥锁（保护共享状态）
    std::condition_variable cv_;                 // 条件变量（线程间通知）

    /* ==================== 数据缓冲区 ==================== */
    std::deque<RawAudioBlock> input_queue_;       // 输入音频块队列（生产者-消费者缓冲）
    std::vector<float> pcm_stream_cache_;         // PCM 流连续缓存（用于滑动窗口提取帧）
    double current_stream_pts_ = 0.0;            // 当前缓存音频流的起始时间戳
    std::deque<std::vector<float>> mel_frame_buffer_;  // Mel 特征帧环形缓冲区

    /* ==================== 状态追踪变量 ==================== */
    int video_frame_index_ = 0;                  // 已生成的视频帧索引（从 0 开始）
    int total_mel_frames_ = 0;                   // 已提取的 Mel 特征帧总数

    // 离线输入结束标志
    std::atomic<bool> input_finished_{false};    // 标记是否所有音频输入已完成

    // 尾部 flush 是否已经执行过，防止重复补帧
    bool tail_flushed_ = false;                  // 防止多次执行尾部视频帧补齐

    // 累计输入音频采样点数，用于精确计算目标视频的帧数
    int64_t total_input_samples_ = 0;            // 用于离线模式下的精确时长计算

    // 实时模式控制变量
    bool realtime_mode_ = false;                 // 是否启用实时模式（允许丢帧降低延迟）
    size_t realtime_max_queue_blocks_ = 20;      // 实时模式下最大队列长度（超过则丢弃旧数据）

    /**
     * @brief 构造函数 - 初始化所有处理组件和参数
     *
     * @param cb 特征输出回调函数
     * @param sr 音频采样率
     *
     * 初始化内容：
     * 1. 计算帧长和步长（基于采样率）
     * 2. 创建预处理器和 Mel 提取器
     * 3. 预计算 Hamming 窗函数系数
     * 4. 预分配 PCM 缓冲区空间（2秒音频）
     */
    Impl(AudioProcessor::FeatureCallback cb, int sr)
        : output_cb_(std::move(cb)), sample_rate_(sr) {

        // 计算帧长：50ms 窗口（16000 Hz 下为 800 样本）
        frame_size_ = static_cast<int>(sr * 50.0 / 1000.0);
        // 计算步长：12.5ms 步进（16000 Hz 下为 200 样本）
        // 这样相邻帧有 75% 的重叠，保证特征平滑性
        stride_size_ = static_cast<int>(sr * 12.5 / 1000.0);

        // 创建音频预处理器（用于预加重等操作）
        preprocessor_ = std::make_unique<AudioPreprocessor>();
        // 创建 Mel 特征提取器
        // 参数: 采样率=16000, FFT点数=800, Mel频带数=80
        mel_extractor_ = std::make_unique<MelFeatureExtractor>(sr, 800, 80);

        // 预计算 Hamming 窗函数系数
        // 公式: w(n) = 0.54 - 0.46 * cos(2πn / (N-1))
        // 作用: 减少频谱泄漏，提高频谱分辨率
        hamming_window_.resize(frame_size_);
        for (int i = 0; i < frame_size_; ++i) {
            hamming_window_[i] = 0.54f - 0.46f * cosf(2.0f * M_PI * i / (frame_size_ - 1));
        }

        // 预分配 PCM 流缓存空间（可容纳 2 秒的音频数据）
        // 减少运行时的动态内存分配，提高性能
        pcm_stream_cache_.reserve(sr * 2);
    }

    /**
     * @brief 析构函数 - 确保线程正确停止
     *
     * RAII 模式：析构时自动调用 stop() 清理资源
     */
    ~Impl() {
        stop();
    }

    /**
     * @brief 标记音频输入已完成（离线模式使用）
     *
     * 调用此方法后：
     * - 设置 input_finished_ 标志为 true
     * - 唤醒工作线程，使其开始处理剩余数据并执行尾部补帧
     */
    void markInputFinished() {
        std::cout << "[AudioProcessor] markInputFinished called." << std::endl;
        // 使用 release 语义确保之前的写入对其他线程可见
        input_finished_.store(true, std::memory_order_release);
        // 唤醒可能正在等待的工作线程
        cv_.notify_all();
    }

    /**
     * @brief 启动后台音频处理线程
     *
     * 执行流程：
     * 1. 如果已在运行，直接返回成功
     * 2. 重置所有内部状态和缓冲区
     * 3. 创建并启动工作线程（运行 processLoop）
     *
     * @return true 启动成功或已运行
     */
    bool start() {
        // 防止重复启动
        if (is_running_.load()) {
            return true;
        }

        {
            // 加锁重置所有状态，确保线程安全
            std::lock_guard<std::mutex> lock(mtx_);
            input_queue_.clear();          // 清空输入队列
            pcm_stream_cache_.clear();      // 清空 PCM 缓存
            mel_frame_buffer_.clear();      // 清空 Mel 特征缓冲区

            // 重置所有时间轴和计数器到初始状态
            video_frame_index_ = 0;
            total_mel_frames_ = 0;
            current_stream_pts_ = 0.0;
            input_finished_.store(false);   // 重置输入结束标志
            tail_flushed_ = false;          // 允许再次执行尾部补帧
            total_input_samples_ = 0;       // 重置采样点计数
        }

        // 重置处理状态标志
        is_processing_.store(false);
        // 设置运行标志为 true（必须在创建线程之前）
        is_running_.store(true);
        // 创建后台工作线程，执行 processLoop 主循环
        worker_thread_ = std::thread(&Impl::processLoop, this);
        std::cout << "[AudioProcess] Start running!" << std::endl;
        return true;
    }

    /**
     * @brief 停止后台处理线程
     *
     * 执行流程：
     * 1. 将 is_running_ 设为 false（原子操作，只执行一次）
     * 2. 唤醒工作线程让其退出循环
     * 3. 阻塞等待线程结束（join）
     *
     * 注意：此方法是阻塞的，会等待当前处理完成
     */
    void stop() {
        // 使用 exchange 确保只停止一次（防止并发调用）
        if (!is_running_.exchange(false)) {
            return;  // 已经停止过了，直接返回
        }

        // 唤醒工作线程（如果它正在 wait 状态）
        cv_.notify_all();
        // 等待工作线程真正结束（阻塞直到线程退出）
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
        std::cout << "[AudioProcess] Stop!" << std::endl;
    }

    /**
     * @brief 设置实时/离线模式
     *
     * @param enabled true=实时模式（允许丢帧）, false=离线模式（保证完整）
     *
     * 模式区别：
     * - 实时模式：当队列过长时丢弃旧数据，降低延迟，适用于直播等场景
     * - 离线模式：保留所有数据，保证完整性，适用于文件处理
     *
     * 切换到实时模式时，会立即清理超出限制的旧队列数据
     */
    void setRealtimeMode(bool enabled) {
        std::lock_guard<std::mutex> lock(mtx_);  // 加锁保护模式切换

        realtime_mode_ = enabled;

        // 切换到实时模式时，清理过长的历史队列
        // 防止之前积累的旧数据影响实时性
        if (realtime_mode_) {
            while (input_queue_.size() > realtime_max_queue_blocks_) {
                input_queue_.pop_front();  // 丢弃最旧的音频块
            }
        }

        std::cout << "[AudioProcessor] realtime_mode="
              << (enabled ? "ON" : "OFF")
              << ", max_queue_blocks=" << realtime_max_queue_blocks_
              << std::endl;
    }

    /**
     * @brief 推送原始 PCM 音频数据到处理队列
     *
     * @param pcm_data PCM 浮点采样数据向量（通常归一化到 [-1, 1]）
     * @param start_pts_ms 该音频块的起始展示时间戳（毫秒）
     *
     * 线程安全性：此方法可从任何线程安全调用
     *
     * 处理逻辑：
     * 1. 检查运行状态和数据有效性
     * 2. 实时模式下，如果队列过长则丢弃旧数据
     * 3. 将数据打包成 RawAudioBlock 并加入输入队列
     * 4. 唤醒工作线程开始处理
     */
    void pushRawAudio(const std::vector<float>& pcm_data, double start_pts_ms) {
        // 快速检查：未运行或空数据直接返回
        if (!is_running_.load() || pcm_data.empty()) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mtx_);  // 加锁保护队列操作

            // 根据当前模式决定是否丢弃旧数据
            // 实时模式：允许丢旧音频块，防止延迟累积
            // 离线模式：不能丢，否则会造成视频截断/少帧
            if (realtime_mode_) {
                // 队列满时丢弃最旧的块（FIFO）
                while (input_queue_.size() >= realtime_max_queue_blocks_) {
                    input_queue_.pop_front();
                }
            }
            // 将音频数据和其时间戳一起入队
            input_queue_.push_back({pcm_data, start_pts_ms});
        }

        // 通知工作线程有新数据到达
        cv_.notify_one();
    }

    /**
     * @brief 检查音频是否已完全处理完毕（离线模式使用）
     *
     * @return true 表示所有数据处理完成，可以安全销毁处理器
     *
     * 判断条件（必须全部满足）：
     * 1. input_finished_ == true: 已调用 markInputFinished()
     * 2. tail_flushed_ == true: 尾部补帧已完成
     * 3. input_queue_ 为空: 所有输入数据已消费完毕
     * 4. is_processing_ == false: 当前没有正在进行的处理操作
     *
     * 使用场景：离线模式下，主线程可轮询此方法等待处理完成
     */
    bool isDrained() const {
        std::lock_guard<std::mutex> lock(mtx_);
        // 四个条件必须同时满足才认为完全排空：
        // - 输入已标记结束
        // - 尾部视频帧补齐已完成
        // - 输入队列已清空
        // - 当前没有正在处理的任务
        return input_finished_.load(std::memory_order_acquire) &&
                tail_flushed_ &&
                input_queue_.empty() &&
                !is_processing_.load();
    }

    /**
     * @brief 从 Mel 帧缓冲区中组装 Wav2Lip 所需的特征块
     *
     * @param relative_idx 在 mel_frame_buffer_ 中的起始相对索引
     * @param allow_padding 是否允许边界 padding（尾部补帧时为 true）
     *
     * @return 1280 维向量（80 个 Mel 频带 × 16 个时间帧），如果数据不足则返回空向量
     *
     * 输出格式说明：
     * - 维度: 80 × 16 = 1280
     * - 80: Mel 频带数量（频率维度）
     * - 16: 连续时间帧数（时间维度，覆盖 200ms 音频：16 × 12.5ms）
     *
     * 边界处理策略：
     * - 首部越界 (idx < 0): 使用第 0 帧（复制首帧）
     * - 尾部越界:
     *   - allow_padding=false: 返回空向量（表示数据不足）
     *   - allow_padding=true: 使用最后一帧（复制尾帧，用于尾部补帧）
     */
    std::vector<float> makeWav2LipChunkFromBuffer(int relative_idx, bool allow_padding) {
        std::vector<float> wav2lip_chunk;
        // 预分配空间：80 个频率 bin × 16 个时间帧 = 1280 维
        wav2lip_chunk.reserve(80 * 16);

        // 缓冲区为空，直接返回空结果
        if (mel_frame_buffer_.empty()) {
            return wav2lip_chunk;
        }

        const int buffer_size = static_cast<int>(mel_frame_buffer_.size());

        // 双重循环组装特征块：外层遍历频率，内层遍历时间
        for (int freq = 0; freq < 80; ++freq) {       // 80 个 Mel 频带
            for (int time = 0; time < 16; ++time) {    // 16 个连续时间帧
                int idx = relative_idx + time;          // 计算绝对索引

                // 首部边界处理：索引小于 0 时，使用第 0 帧
                if (idx < 0) {
                    idx = 0;
                }

                // 尾部边界处理
                if (idx >= buffer_size) {
                    if (!allow_padding) {
                        // 不允许 padding，返回空向量表示数据不足
                        wav2lip_chunk.clear();
                        return wav2lip_chunk;
                    }

                    // 允许 padding：复用最后一帧 Mel 特征（用于尾部补齐）
                    idx = buffer_size - 1;
                }

                // 将对应位置的 Mel 特征值加入输出向量
                wav2lip_chunk.push_back(mel_frame_buffer_[idx][freq]);
            }
        }

        return wav2lip_chunk;
    }

    /**
     * @brief 刷新剩余视频帧（离线模式尾部补帧）
     *
     * 此方法在音频输入结束后调用，用于补齐最后的视频帧。
     *
     * 工作原理：
     * - 根据总输入采样点数计算音频总时长
     * - 按 25FPS 计算应该生成的视频帧总数
     * - 补齐从当前帧到目标帧数之间的所有视频帧
     * - 使用 Mel 缓冲区最后一帧进行 padding
     *
     * 重要说明：
     * - 实时模式下直接跳过（不执行尾部补帧）
     * - 只执行一次（通过 tail_flushed_ 标志保护）
     * - 保证输出视频长度与音频时长精确匹配
     */
    void flushRemainingVideoFrames() {
        // 实时模式：跳过尾部补帧，直接标记完成
        // 实时场景下不需要保证完整性，低延迟更重要
        if (realtime_mode_) {
            tail_flushed_ = true;
            std::cout << "[AudioProcessor] realtime mode: skip tail flush." << std::endl;
            return;
        }

        // 打印当前状态信息，便于调试
        std::cout << "[AudioProcessor] flushRemainingVideoFrames called. "
          << "mel_buffer=" << mel_frame_buffer_.size()
          << ", total_input_samples=" << total_input_samples_
          << ", video_frame_index=" << video_frame_index_
          << ", input_finished=" << input_finished_.load()
          << std::endl;

        // 防止重复执行：如果已经 flush 过，直接返回
        if (tail_flushed_) {
            return;
        }

        // 安全检查：没有有效数据时跳过
        if (mel_frame_buffer_.empty() || total_input_samples_ <= 0) {
            std::cout << "[AudioProcessor] Flush skipped: empty mel buffer or no input samples."
                    << std::endl;
            return;
        }

        /* ========== 计算目标视频帧数 ========== */
        const double video_frame_ms = 40.0;  // 视频帧间隔：25 FPS → 每帧 40ms

        // 根据总采样点数计算音频总时长（毫秒）
        double audio_duration_ms =
            static_cast<double>(total_input_samples_) / sample_rate_ * 1000.0;

        // 向上取整计算目标视频帧总数（确保覆盖完整音频）
        int target_total_video_frames =
            static_cast<int>(std::ceil(audio_duration_ms / video_frame_ms));

        // 计算 Mel 缓冲区的头部在全局 Mel 帧序列中的绝对索引
        int current_buffer_head_idx =
            total_mel_frames_ - static_cast<int>(mel_frame_buffer_.size());

        // 打印详细调试信息
        std::cout << "[AudioProcessor] Flush tail frames: "
                << "audio_duration_ms=" << audio_duration_ms
                << ", current_video_frames=" << video_frame_index_
                << ", target_total_video_frames=" << target_total_video_frames
                << ", total_mel_frames=" << total_mel_frames_
                << ", buffer_size=" << mel_frame_buffer_.size()
                << std::endl;

        /* ========== 循环生成剩余视频帧 ========== */
        while (video_frame_index_ < target_total_video_frames) {
            // 计算当前视频帧对应的起始 Mel 帧索引（比例 3.2:1）
            int expected_mel_start =
                static_cast<int>(video_frame_index_ * 3.2);

            // 转换为缓冲区内的相对索引
            int relative_idx = expected_mel_start - current_buffer_head_idx;

            // 边界钳位：确保索引在合法范围内
            if (relative_idx < 0) {
                relative_idx = 0;  // 不能小于缓冲区开头
            }
            if (relative_idx >= static_cast<int>(mel_frame_buffer_.size())) {
                relative_idx = static_cast<int>(mel_frame_buffer_.size()) - 1;  // 不能超过末尾
            }

            // 组装 Wav2Lip 特征块（允许 padding，使用最后一帧填充不足部分）
            std::vector<float> wav2lip_chunk =
                makeWav2LipChunkFromBuffer(relative_idx, true);  // true=允许尾部padding

            // 验证输出维度是否正确（必须是 1280 维）
            if (wav2lip_chunk.size() != 1280) {
                std::cerr << "[AudioProcessor] Flush failed: invalid chunk size="
                        << wav2lip_chunk.size() << std::endl;
                break;  // 维度错误，终止补帧
            }

            // 计算当前视频帧的时间戳
            double pts_ms = video_frame_index_ * video_frame_ms;

            // 通过回调函数输出特征数据给推理引擎
            if (output_cb_) {
                output_cb_(pts_ms, std::move(wav2lip_chunk));
            }

            // 打印补帧进度信息
            std::cout << "[AudioProcessor] Flush video frame idx="
                    << video_frame_index_
                    << ", pts=" << pts_ms << " ms"
                    << std::endl;

            // 推进到下一个视频帧
            video_frame_index_++;
        }

        // 标记尾部补帧已完成
        tail_flushed_ = true;
    }
    /**
     * @brief 工作线程主循环 - 音频处理的核心逻辑
     *
     * 这是后台线程的入口函数，负责：
     * 1. 从输入队列获取音频数据块
     * 2. 将数据追加到 PCM 流缓存
     * 3. 使用滑动窗口提取音频帧
     * 4. 对每帧进行预处理和 Mel 特征提取
     * 5. 按 25FPS 视频帧率组装 Wav2Lip 特征块并输出
     *
     * 循环终止条件：
     * - is_running_ 变为 false（调用 stop()）
     * - 输入队列为空 且 input_finished_ 为 true（离线模式正常结束）
     */
    void processLoop() {
        // 主循环：持续运行直到停止信号或输入完成
        while (is_running_.load(std::memory_order_acquire)) {

            /* ========== 第一阶段：从队列获取数据 ========== */
            std::vector<RawAudioBlock> blocks_to_process;  // 本批次要处理的数据块
            {
                // 加锁等待新数据或终止信号
                std::unique_lock<std::mutex> lock(mtx_);

                // 条件变量等待：最多等 50ms 就超时检查一次
                // 唤醒条件：有新数据 OR 输入结束 OR 需要停止
                cv_.wait_for(lock, std::chrono::milliseconds(50), [this] {
                    return !input_queue_.empty()
                            || input_finished_.load(std::memory_order_acquire)
                            || !is_running_.load();
                });

                // 检查是否需要退出循环
                if (!is_running_.load()) {
                    break;  // 收到停止信号，立即退出
                }

                // 检查是否输入已结束且队列为空
                if (input_queue_.empty()) {
                    if (input_finished_.load(std::memory_order_acquire)) {
                        break;  // 正常结束：所有数据处理完毕
                    }
                    continue;  // 超时但还没结束，继续等待
                }

                // 批量取出队列中的所有数据块（减少锁竞争）
                while (!input_queue_.empty()) {
                    blocks_to_process.push_back(std::move(input_queue_.front()));
                    input_queue_.pop_front();
                }

                // 标记开始处理（用于 isDrained 判断）
                is_processing_.store(true);
            }

            /* ========== 第二阶段：将数据块合并到 PCM 流缓存 ========== */
            for (const auto& block : blocks_to_process) {
                // 记录第一个块的起始时间戳作为当前流的基准时间
                if (pcm_stream_cache_.empty()) {
                    current_stream_pts_ = block.start_pts;
                }

                // 累计总采样点数（用于计算目标视频帧数）
                total_input_samples_ += static_cast<int64_t>(block.data.size());

                // 将 PCM 数据追加到连续缓存
                pcm_stream_cache_.insert(pcm_stream_cache_.end(),
                                         block.data.begin(), block.data.end());
            }

            /* ========== 第三阶段：滑动窗口提取和处理音频帧 ========== */
            // 只要缓存中有足够的数据（>= frame_size_），就持续提取帧
            while (pcm_stream_cache_.size() >= static_cast<size_t>(frame_size_)) {

                // 提取当前帧：从缓存头部取 frame_size_ 个样本
                std::vector<float> current_frame(
                    pcm_stream_cache_.begin(),
                    pcm_stream_cache_.begin() + frame_size_);

                /* ----- 步骤1: 预加重（Pre-emphasis）----- */
                // 高通滤波器，补偿高频分量能量损失
                // 公式: y[n] = x[n] - α * x[n-1], α 通常取 0.95-0.97
                preprocessor_->preEmphasize(current_frame, 0.97f);

                /* ----- 步骤2: 加窗（Windowing）----- */
                // 应用 Hamming 窗减少频谱泄漏
                // 逐点相乘：frame[i] *= window[i]
                for (int i = 0; i < frame_size_; ++i) {
                    current_frame[i] *= hamming_window_[i];
                }

                /* ----- 步骤3: Mel 特征提取 ----- */
                try {
                    // 调用 Mel 特征提取器，返回 80 维 Mel 频谱向量
                    cv::Mat mel_feature = mel_extractor_->extract(current_frame);
                    if (!mel_feature.empty()) {
                        // 将 OpenCV Mat 转换为标准 vector
                        std::vector<float> feature_vec(
                            mel_feature.begin<float>(),
                            mel_feature.end<float>());

                        /* ===== 核心：基于视频帧率的严格映射算法 ===== */

                        // 将新生成的 Mel 特征加入缓冲区
                        mel_frame_buffer_.push_back(std::move(feature_vec));
                        total_mel_frames_++;  // 更新总帧计数

                        /*
                         * 音视频同步映射规则：
                         * - 视频帧率: 25 FPS → 每帧间隔 40ms
                         * - 音频 Hop: 12.5ms（步长对应的时长）
                         * - 映射比例: 40 / 12.5 = 3.2
                         *   即每个视频帧对应 3.2 个 Mel 帧
                         *   每个视频帧需要 16 个连续 Mel 帧组成一个 chunk
                         */
                        while (true) {
                            // 计算当前视频帧应该从哪个绝对 Mel 索引开始
                            int expected_mel_start =
                                static_cast<int>(video_frame_index_ * 3.2);

                            // 计算缓冲区头部的绝对索引
                            int current_buffer_head_idx =
                                total_mel_frames_ - mel_frame_buffer_.size();

                            // 转换为相对索引（在当前缓冲区中的位置）
                            int relative_idx = expected_mel_start - current_buffer_head_idx;

                            // 检查是否有足够的数据来组装一个完整的 chunk
                            // 条件：起始索引合法 AND 向后有足够的 16 帧
                            if (relative_idx >= 0 &&
                                relative_idx + 16 <= static_cast<int>(mel_frame_buffer_.size())) {

                                // 组装 Wav2Lip 特征块（不允许 padding）
                                std::vector<float> wav2lip_chunk =
                                    makeWav2LipChunkFromBuffer(relative_idx, false);  // false=不允许padding

                                // 验证输出维度
                                if (wav2lip_chunk.size() != 1280) {
                                    break;  // 维度错误，跳出循环
                                }

                                // 调试输出：前 10 个 chunk 的统计信息
                                static int debug_mel_count = 0;
                                if (debug_mel_count < 10) {
                                    auto minmax = std::minmax_element(wav2lip_chunk.begin(),
                                                                    wav2lip_chunk.end());
                                    double sum = 0.0;
                                    for (float v : wav2lip_chunk) {
                                        sum += v;
                                    }

                                    std::cout << "[DEBUG MEL CHUNK] idx=" << debug_mel_count
                                            << " size=" << wav2lip_chunk.size()
                                            << " min=" << *minmax.first
                                            << " max=" << *minmax.second
                                            << " mean=" << sum / wav2lip_chunk.size()
                                            << std::endl;

                                    debug_mel_count++;
                                }

                                // 计算当前视频帧的时间戳（毫秒）
                                double pts_ms = video_frame_index_ * 40.0;

                                // 通过回调函数输出特征给推理引擎
                                if (output_cb_) {
                                    output_cb_(pts_ms, std::move(wav2lip_chunk));
                                }

                                // 推进到下一个视频帧
                                video_frame_index_++;
                            } else {
                                // 数据不足：
                                // - 还没达到当前视频帧需要的起始位置，或者
                                // - 缓冲区中没有足够的 16 帧
                                // → 跳出循环，等待更多 Mel 帧到来
                                break;
                            }
                        }

                        /* ===== 缓冲区管理：清理不再需要的旧数据 ===== */
                        // 计算下一个视频帧需要的起始位置
                        int next_mel_start = static_cast<int>(video_frame_index_ * 3.2);
                        int current_buf_head = total_mel_frames_ - mel_frame_buffer_.size();

                        // 计算可以安全弹出的帧数
                        int pop_count = next_mel_start - current_buf_head;

                        if (pop_count > 0) {
                            // 至少保留一帧！供尾部 padding 使用
                            int max_pop = std::max(0,
                                static_cast<int>(mel_frame_buffer_.size()) - 1);
                            // 取实际可弹出数量和最大允许值的最小值
                            pop_count = std::min(pop_count, max_pop);

                            // 弹出旧的 Mel 帧（释放内存）
                            for (int i = 0; i < pop_count; i++) {
                                mel_frame_buffer_.pop_front();
                            }
                        }
                        /* ============================================================ */
                    }
                } catch (const std::exception& e) {
                    // Mel 提取异常处理：打印错误但不中断处理流程
                    std::cerr << "[AudioProcessor] Error extracting Mel feature: "
                              << e.what() << std::endl;
                }

                /* ----- 步骤4: 移动窗口 ----- */
                // 从缓存中删除已经处理过的样本（步长大小）
                // 实现滑动窗口效果：下一次循环会处理新的帧
                pcm_stream_cache_.erase(
                    pcm_stream_cache_.begin(),
                    pcm_stream_cache_.begin() + stride_size_);

                // 更新当前时间戳（加上步长对应的时间）
                current_stream_pts_ +=
                    (static_cast<double>(stride_size_) / sample_rate_) * 1000.0;
            }

            /* ========== 第四阶段：状态报告（每 100 次循环输出一次） ========== */
            static int status_count = 0;
            if (++status_count % 100 == 0) {
                std::cout << "[AudioProcessor] status. "
                        << "input_finished=" << input_finished_.load()
                        << ", input_queue=" << input_queue_.size()
                        << ", pcm_cache=" << pcm_stream_cache_.size()
                        << ", mel_buffer=" << mel_frame_buffer_.size()
                        << ", total_input_samples=" << total_input_samples_
                        << ", video_frame_index=" << video_frame_index_
                        << std::endl;
            }

            // 标记本轮处理完成
            is_processing_.store(false);
        }

        /* ========== 第五阶段：尾部补帧（仅离线模式） ========== */
        // 主循环结束后，调用尾部补帧函数补齐剩余的视频帧
        flushRemainingVideoFrames();

        // 最终标记处理完成
        is_processing_.store(false);
    }
};

/* ======================================================================
 *  公共接口实现（委托给 Impl 类）
 *  这些方法只是简单的转发，真正的逻辑在 Impl 中
 * ====================================================================== */

/**
 * @brief 构造函数 - 创建音频处理器实例
 *
 * @param output_cb 特征输出回调函数，当生成 Wav2Lip 特征块时调用
 * @param sample_rate 音频采样率（默认 16000 Hz）
 *
 * 使用 PImpl 模式：将所有实现细节隐藏在 Impl 类中
 */
AudioProcessor::AudioProcessor(AudioProcessor::FeatureCallback output_cb, int sample_rate)
    : pImpl(std::make_unique<Impl>(std::move(output_cb), sample_rate)) {}

/**
 * @brief 析构函数 - 自动停止后台线程并释放资源
 */
AudioProcessor::~AudioProcessor() = default;  // unique_ptr<Impl> 会自动调用 ~Impl()

/**
 * @brief 启动后台处理线程
 *
 * @return true 成功启动或已在运行
 *
 * 线程安全：可从任何线程调用
 */
bool AudioProcessor::start() {
    return pImpl->start();  // 委托给 Impl
}

/**
 * @brief 停止后台处理线程（阻塞等待）
 *
 * 调用后会阻塞直到当前处理完成且线程退出
 * 线程安全：可从任何线程调用
 */
void AudioProcessor::stop() {
    pImpl->stop();  // 委托给 Impl
}

/**
 * @brief 推送原始 PCM 音频数据
 *
 * @param pcm_data PCM 浮点采样数据向量
 * @param start_pts_ms 该音频块的起始时间戳（毫秒）
 *
 * 线程安全：可从任何线程调用（包括音频采集回调线程）
 */
void AudioProcessor::pushRawAudio(const std::vector<float>& pcm_data, double start_pts_ms) {
    pImpl->pushRawAudio(pcm_data, start_pts_ms);  // 委托给 Impl
}

/**
 * @brief 检查是否所有数据处理完毕（离线模式）
 *
 * @return true 表示可以安全销毁处理器
 *
 * 典型用法：
 * @code
 *   processor->markInputFinished();
 *   while (!processor->isDrained()) {
 *       std::this_thread::sleep_for(10ms);
 *   }
 *   // 现在可以安全销毁 processor
 * @endcode
 */
bool AudioProcessor::isDrained() const {
    return pImpl->isDrained();  // 委托给 Impl
}

/**
 * @brief 标记音频输入已完成（离线模式必须调用）
 *
 * 通知处理器不会再有新的音频数据推送，
 * 处理器将开始处理剩余缓存并执行尾部补帧
 */
void AudioProcessor::markInputFinished() {
    pImpl->markInputFinished();  // 委托给 Impl
}

/**
 * @brief 设置实时/离线模式
 *
 * @param enabled true=实时模式（允许丢帧）, false=离线模式（保证完整）
 *
 * 可在运行时动态切换模式
 */
void AudioProcessor::setRealtimeMode(bool enabled) {
    pImpl->setRealtimeMode(enabled);  // 委托给 Impl
}

} // namespace Audio
} // namespace DigitalHuman