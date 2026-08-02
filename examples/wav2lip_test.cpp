#include <iostream>
#include <vector>
#include <cmath>
#include <string>
#include <opencv2/opencv.hpp>
#include <ncnn/net.h>

#include "audio/audio_loader.h"
#include "audio/audio_framer.h"
#include "audio/audio_mel_feature_extract.h"

using namespace DigitalHuman::Audio;

// Wav2Lip 输入规格
static const int FACE_SIZE  = 96;   // 人脸 96x96
static const int MEL_BINS   = 80;   // 梅尔滤波器数量
static const int MEL_STEPS  = 16;   // 每帧视频对应 16 帧梅尔

int main(int argc, char** argv) {
    std::cout << "=== Digital Human SDK: Wav2Lip ncnn Model Test ===" << std::endl;

    std::string model_dir = (argc > 1) ? argv[1] : "../../models/wav2lip";
    std::string face_path = (argc > 2) ? argv[2] : "../../aligned_face_96x96.jpg";
    std::string wav_path  = (argc > 3) ? argv[3] : "../../test_audio.wav";

    // ---------------------------------------------------------------- 1. 加载模型
    ncnn::Net net;
    net.opt.use_vulkan_compute = false;
    net.opt.num_threads = 4;

    std::string param_path = model_dir + "/wav2lip.param";
    std::string bin_path   = model_dir + "/wav2lip.bin";

    if (net.load_param(param_path.c_str()) != 0) {
        std::cerr << "[FAIL] load_param failed: " << param_path << std::endl;
        return -1;
    }
    if (net.load_model(bin_path.c_str()) != 0) {
        std::cerr << "[FAIL] load_model failed: " << bin_path << std::endl;
        return -1;
    }
    std::cout << "[OK] Model loaded: " << param_path << std::endl;

    // ---------------------------------------------------------------- 2. 准备人脸输入 (6x96x96)
    cv::Mat face_bgr = cv::imread(face_path, cv::IMREAD_COLOR);
    if (face_bgr.empty()) {
        std::cerr << "[FAIL] read face image failed: " << face_path << std::endl;
        return -1;
    }
    if (face_bgr.cols != FACE_SIZE || face_bgr.rows != FACE_SIZE) {
        cv::resize(face_bgr, face_bgr, cv::Size(FACE_SIZE, FACE_SIZE));
    }
    std::cout << "[Info] Face loaded: " << face_path
              << " (" << face_bgr.cols << "x" << face_bgr.rows << ")" << std::endl;

    // Wav2Lip 输入通道顺序: [masked(RGB) , reference(RGB)]，数值范围 [0,1]
    // masked = 下半张脸置零 (待生成的嘴部区域)
    ncnn::Mat face_in(FACE_SIZE, FACE_SIZE, 6);
    for (int y = 0; y < FACE_SIZE; ++y) {
        const cv::Vec3b* row = face_bgr.ptr<cv::Vec3b>(y);
        for (int x = 0; x < FACE_SIZE; ++x) {
            float r = row[x][2] / 255.0f;
            float g = row[x][1] / 255.0f;
            float b = row[x][0] / 255.0f;
            bool masked = (y >= FACE_SIZE / 2);   // 下半部分遮挡

            face_in.channel(0)[y * FACE_SIZE + x] = masked ? 0.0f : r;
            face_in.channel(1)[y * FACE_SIZE + x] = masked ? 0.0f : g;
            face_in.channel(2)[y * FACE_SIZE + x] = masked ? 0.0f : b;
            face_in.channel(3)[y * FACE_SIZE + x] = r;   // reference 保留完整人脸
            face_in.channel(4)[y * FACE_SIZE + x] = g;
            face_in.channel(5)[y * FACE_SIZE + x] = b;
        }
    }

    // ---------------------------------------------------------------- 3. 准备音频输入 (1x80x16)
    std::vector<int16_t> pcm_i16;
    AudioLoader loader(16000);
    std::vector<float> pcm;
    if (loader.load(wav_path, pcm_i16)) {
        pcm.resize(pcm_i16.size());
        for (size_t i = 0; i < pcm_i16.size(); ++i) pcm[i] = pcm_i16[i] / 32768.0f;
        std::cout << "[Info] Audio loaded: " << wav_path
                  << " (" << pcm.size() << " samples)" << std::endl;
    } else {
        std::cerr << "[Warn] audio load failed, fall back to 440Hz sine." << std::endl;
        pcm.resize(16000);
        for (int i = 0; i < 16000; ++i)
            pcm[i] = 0.6f * sinf(2 * 3.14159f * 440 * i / 16000.0f);
    }

    // Wav2Lip 官方参数: win 800 (50ms), hop 200 (12.5ms), n_mels 80
    AudioFramer framer(16000, 50, 12.5, WindowType::Hamming);
    auto frames = framer.process(pcm);
    MelFeatureExtractor mel_extractor(16000, 800, 80);
    cv::Mat mel = mel_extractor.extractBatch(frames);   // [N, 80]
    std::cout << "[Info] Mel spectrogram: " << mel.rows << "x" << mel.cols << std::endl;

    if (mel.rows < MEL_STEPS || mel.cols != MEL_BINS) {
        std::cerr << "[FAIL] mel shape mismatch, need >=" << MEL_STEPS
                  << "x" << MEL_BINS << std::endl;
        return -1;
    }

    // 取中间一段 16 帧，转成 ncnn 的 (w=16, h=80, c=1)
    int start = (mel.rows - MEL_STEPS) / 2;
    ncnn::Mat mel_in(MEL_STEPS, MEL_BINS, 1);
    for (int b = 0; b < MEL_BINS; ++b)
        for (int t = 0; t < MEL_STEPS; ++t)
            mel_in.channel(0)[b * MEL_STEPS + t] = mel.at<float>(start + t, b);

    // ---------------------------------------------------------------- 4. 推理
    double t0 = (double)cv::getTickCount();
    ncnn::Extractor ex = net.create_extractor();
    if (ex.input("face", face_in) != 0) { std::cerr << "[FAIL] input face" << std::endl; return -1; }
    if (ex.input("mel",  mel_in)  != 0) { std::cerr << "[FAIL] input mel"  << std::endl; return -1; }

    ncnn::Mat out;
    if (ex.extract("pred", out) != 0) { std::cerr << "[FAIL] extract pred" << std::endl; return -1; }
    double ms = ((double)cv::getTickCount() - t0) / cv::getTickFrequency() * 1000.0;

    std::cout << "[OK] Inference done in " << ms << " ms" << std::endl;
    std::cout << "[Result] Output shape: c=" << out.c
              << " h=" << out.h << " w=" << out.w << std::endl;

    if (out.empty() || out.c != 3 || out.h != FACE_SIZE || out.w != FACE_SIZE) {
        std::cerr << "[FAIL] unexpected output shape (expect 3x96x96)" << std::endl;
        return -1;
    }

    // ---------------------------------------------------------------- 5. 数值健康检查
    float vmin = 1e9f, vmax = -1e9f, vsum = 0.0f;
    int nan_cnt = 0;
    int total = out.c * out.h * out.w;
    for (int c = 0; c < out.c; ++c) {
        const float* p = out.channel(c);
        for (int i = 0; i < out.h * out.w; ++i) {
            float v = p[i];
            if (std::isnan(v) || std::isinf(v)) { nan_cnt++; continue; }
            vmin = std::min(vmin, v);
            vmax = std::max(vmax, v);
            vsum += v;
        }
    }
    std::cout << "[Check] min=" << vmin << " max=" << vmax
              << " mean=" << (vsum / total) << " nan/inf=" << nan_cnt << std::endl;

    if (nan_cnt > 0) { std::cerr << "[FAIL] output contains NaN/Inf" << std::endl; return -1; }
    if (vmax - vmin < 1e-4f) { std::cerr << "[FAIL] output is constant" << std::endl; return -1; }

    // ---------------------------------------------------------------- 6. 保存结果图
    cv::Mat result(FACE_SIZE, FACE_SIZE, CV_8UC3);
    for (int y = 0; y < FACE_SIZE; ++y) {
        for (int x = 0; x < FACE_SIZE; ++x) {
            float r = out.channel(0)[y * FACE_SIZE + x];
            float g = out.channel(1)[y * FACE_SIZE + x];
            float b = out.channel(2)[y * FACE_SIZE + x];
            result.at<cv::Vec3b>(y, x) = cv::Vec3b(
                cv::saturate_cast<uchar>(b * 255.0f),
                cv::saturate_cast<uchar>(g * 255.0f),
                cv::saturate_cast<uchar>(r * 255.0f));
        }
    }
    cv::imwrite("wav2lip_output.jpg", result);

    // 对比图: 原图 | 遮挡图 | 生成图
    cv::Mat masked_vis = face_bgr.clone();
    masked_vis(cv::Rect(0, FACE_SIZE / 2, FACE_SIZE, FACE_SIZE / 2)).setTo(cv::Scalar(0, 0, 0));
    cv::Mat compare;
    cv::hconcat(std::vector<cv::Mat>{face_bgr, masked_vis, result}, compare);
    cv::resize(compare, compare, cv::Size(), 3, 3, cv::INTER_NEAREST);
    cv::imwrite("wav2lip_compare.jpg", compare);

    std::cout << "[Success] Saved wav2lip_output.jpg / wav2lip_compare.jpg" << std::endl;

    // ---------------------------------------------------------------- 7. 验证 mel 分支真正生效
    // 用两组极端 mel (静音 -4 / 满量程 +4) 推理，嘴部区域应有明显差异
    auto infer = [&](const ncnn::Mat& m) {
        ncnn::Extractor e = net.create_extractor();
        e.input("face", face_in);
        e.input("mel", m);
        ncnn::Mat o;
        e.extract("pred", o);
        cv::Mat img(FACE_SIZE, FACE_SIZE, CV_8UC3);
        for (int y = 0; y < FACE_SIZE; ++y)
            for (int x = 0; x < FACE_SIZE; ++x)
                img.at<cv::Vec3b>(y, x) = cv::Vec3b(
                    cv::saturate_cast<uchar>(o.channel(2)[y * FACE_SIZE + x] * 255.0f),
                    cv::saturate_cast<uchar>(o.channel(1)[y * FACE_SIZE + x] * 255.0f),
                    cv::saturate_cast<uchar>(o.channel(0)[y * FACE_SIZE + x] * 255.0f));
        return img;
    };
    auto mouth_diff = [&](const cv::Mat& a, const cv::Mat& b) {
        cv::Rect roi(0, FACE_SIZE / 2, FACE_SIZE, FACE_SIZE / 2);
        cv::Mat ga, gb, d;
        cv::cvtColor(a(roi), ga, cv::COLOR_BGR2GRAY);
        cv::cvtColor(b(roi), gb, cv::COLOR_BGR2GRAY);
        cv::absdiff(ga, gb, d);
        return (float)cv::mean(d)[0];
    };

    ncnn::Mat mel_lo(MEL_STEPS, MEL_BINS, 1); mel_lo.fill(-4.0f);
    ncnn::Mat mel_hi(MEL_STEPS, MEL_BINS, 1); mel_hi.fill(4.0f);
    cv::Mat img_lo = infer(mel_lo), img_hi = infer(mel_hi);
    float sens = mouth_diff(img_lo, img_hi);
    std::cout << "[Check] mel sensitivity (mel=-4 vs mel=+4) mouth diff = " << sens << std::endl;

    if (sens < 2.0f) {
        std::cerr << "[FAIL] mouth does not react to mel input" << std::endl;
        return -1;
    }

    // 真实音频不同时间点的嘴型变化 (信息性: 反映音频/梅尔管线的时间动态)
    std::vector<int> offsets = {0, mel.rows / 4, mel.rows / 2, mel.rows - MEL_STEPS};
    std::vector<cv::Mat> strip = {img_lo, img_hi};
    cv::Mat prev;
    float real_max = 0.0f;

    for (size_t k = 0; k < offsets.size(); ++k) {
        ncnn::Mat m(MEL_STEPS, MEL_BINS, 1);
        for (int b = 0; b < MEL_BINS; ++b)
            for (int t = 0; t < MEL_STEPS; ++t)
                m.channel(0)[b * MEL_STEPS + t] = mel.at<float>(offsets[k] + t, b);

        cv::Mat img = infer(m);
        strip.push_back(img);
        if (!prev.empty()) real_max = std::max(real_max, mouth_diff(img, prev));
        prev = img;
    }
    std::cout << "[Info] real-audio mouth variation (max frame-to-frame) = " << real_max << std::endl;
    if (real_max < 1.0f) {
        std::cout << "[Warn] 真实音频驱动的嘴型变化很小 —— 模型本身正常，"
                     "但当前 MelFeatureExtractor 输出的时间动态过弱" << std::endl;
    }

    cv::Mat seq;
    cv::hconcat(strip, seq);
    cv::resize(seq, seq, cv::Size(), 3, 3, cv::INTER_NEAREST);
    cv::imwrite("wav2lip_audio_driven.jpg", seq);
    std::cout << "[Success] Saved wav2lip_audio_driven.jpg (mel=-4 | mel=+4 | 4x real audio)" << std::endl;

    std::cout << "=== Wav2Lip model is USABLE ===" << std::endl;
    return 0;
}
