#include <iostream>
#include <algorithm>
#include <cstring>
#include <opencv2/opencv.hpp>

#include "model/input_processor.h"

namespace DigitalHuman 
{
namespace Model
{

struct InputProcessor::Impl
{
    const int TARGET_SIZE = 96;   // 神经网络输入尺寸：96x96像素
    const int MEL_H = 80;         // Mel频谱图：80个频率bins
    const int MEL_W = 16;         // Mel频谱图：16个时间帧

    // 图像处理：将人脸图像转为6通道张量（用于数字人唇形合成）
    // 返回值结构：[6, 96, 96] - 前3通道masked图（嘴部遮黑），后3通道原图
    ncnn::Mat processImage(const cv::Mat& image, const cv::Rect& mouth_roi)
    {
        // 空图像检查
        if(image.empty())
        {
            return ncnn::Mat();
        }

        // 步骤1: 缩放到网络要求的固定尺寸96x96
        cv::Mat resized_img;
        if(image.rows != TARGET_SIZE || image.cols != TARGET_SIZE)
        {
            cv::resize(image, resized_img, cv::Size(TARGET_SIZE, TARGET_SIZE));
        }
        else
        {
            resized_img = image.clone();  // 已是目标尺寸，深拷贝避免修改原图
        }

        // 步骤2: 确保图像格式为BGR三通道8位无符号整数
        if(resized_img.type() != CV_8UC3)
        {
            resized_img.convertTo(resized_img, CV_8UC3);
        }

        // 步骤3: 创建mask副本，用于遮罩嘴部区域
        // 核心思想：让网络看到"没有嘴部"的脸，学习根据音频生成嘴部动作
        cv::Mat masked_img = resized_img.clone();

        // 步骤4: 设置嘴部区域的ROI（感兴趣区域）
        cv::Rect roi = mouth_roi;
        if(roi.width <= 0 || roi.height <= 0)
        {
            // 如果传入的ROI无效，默认使用图像下半部分作为嘴部区域
            roi = cv::Rect(0, TARGET_SIZE / 2, TARGET_SIZE, TARGET_SIZE / 2);
        }

        // 步骤5: 裁剪ROI防止越界，然后将嘴部区域像素置黑(0,0,0)
        roi &= cv::Rect(0, 0, TARGET_SIZE, TARGET_SIZE);  // 计算交集确保不越界
        if(roi.width > 0 && roi.height > 0)
        {
            masked_img(roi).setTo(cv::Scalar(0, 0, 0));  // BGR三通道都设为0
        }

        // 步骤6: 转换为NCNN推理框架的矩阵格式
        // PIXEL_BGR表示保持OpenCV的BGR顺序不变（不转RGB）
        ncnn::Mat ncnn_masked = ncnn::Mat::from_pixels(
            masked_img.data, ncnn::Mat::PIXEL_BGR, TARGET_SIZE, TARGET_SIZE);

        ncnn::Mat ncnn_org = ncnn::Mat::from_pixels(
            resized_img.data, ncnn::Mat::PIXEL_BGR, TARGET_SIZE, TARGET_SIZE);

        // 步骤7: 像素值归一化 [0,255] -> [0.0,1.0]
        // 这是深度学习的标准预处理，有助于训练收敛和数值稳定性
        const float norm_vals[3] = {1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f};
        
        // substract_mean_normalize参数：
        //   第1个nullptr: 不减去均值（mean normalization）
        //   第2个norm_vals: 像素值乘以归一化因子
        ncnn_masked.substract_mean_normalize(nullptr, norm_vals);
        ncnn_org.substract_mean_normalize(nullptr, norm_vals);

        // 步骤8: 构建最终的6通道输入张量
        // NCNN Mat构造参数：(width, height, channels, element_size=4bytes for float)
        ncnn::Mat input_tensor(TARGET_SIZE, TARGET_SIZE, 6, (size_t)4u);

        // 填充前3通道：归一化后的masked人脸（嘴部被遮黑）
        for (int c = 0; c < 3; ++c)  // c=0:B, c=1:G, c=2:R
        {
            // 使用memcpy高效复制每个通道的数据（96*96个float）
            std::memcpy(input_tensor.channel(c),
                        ncnn_masked.channel(c),
                        TARGET_SIZE * TARGET_SIZE * sizeof(float));
        }

        // 填充后3通道：归一化后的原始完整人脸
        for (int c = 0; c < 3; ++c) 
        {
            std::memcpy(input_tensor.channel(c + 3),  // 偏移3个通道
                        ncnn_org.channel(c),
                        TARGET_SIZE * TARGET_SIZE * sizeof(float));
        }

        // 最终张量用途：
        // - masked图让网络知道"哪里需要生成"
        // - 原始图提供面部特征、光照等参考信息
        return input_tensor;
    }

    // 音频处理：将Mel频谱特征转为NCNN张量
    // 输入：1280个float值（80频率bins × 16时间帧的一维数组）
    // 输出：NCNN张量 [width=16, height=80, channels=1]
    ncnn::Mat processAudio(const std::vector<float>& mel_features)
    {
        // 验证数据尺寸：必须是80×16=1280个元素
        if(mel_features.size() != 16 * 80)
        {
            std::cerr << "[InputProcessor] Invalid mel size: "
                    << mel_features.size() << ", expected 1280." << std::endl;
            return ncnn::Mat();
        }

        // 创建音频张量：宽=16（时间轴），高=80（频率轴），单通道
        ncnn::Mat audio_tensor(16, 80, 1, (size_t)4u);

        // 将一维vector重新排列成二维矩阵（频率×时间）
        // 外层循环遍历频率维度（80个频率bin）
        for (int freq = 0; freq < 80; ++freq) 
        {
            float* row_ptr = audio_tensor.row(freq);  // 获取当前频率行的指针
            
            // 内层循环遍历时间维度（16个时间帧）
            for (int time = 0; time < 16; ++time) 
            {
                // 一维索引计算：freq * 16 + time
                row_ptr[time] = mel_features[freq * 16 + time];
            }
        }
        // 数据布局示例：
        // mel[0~15]   -> 第0行（最低频）的16帧
        // mel[16~31]  -> 第1行
        // ...
        // mel[1264~1279] -> 第79行（最高频）

        return audio_tensor;
    }
};

// 构造函数：使用PImpl模式，unique_ptr自动管理内存
InputProcessor::InputProcessor() : pImpl(std::make_unique<Impl>()) {}
InputProcessor::~InputProcessor() = default;

// 公共接口：委托给Impl实现（隐藏内部细节）
ncnn::Mat InputProcessor::processImage(const cv::Mat& image, const cv::Rect& mouth_roi)
{
    return pImpl->processImage(image, mouth_roi);
}

ncnn::Mat InputProcessor::processAudio(const std::vector<float>& mel_data)
{
    return pImpl->processAudio(mel_data);
}

// 张量验证工具：检查输出张量的形状是否符合预期
bool InputProcessor::validateTensor(const ncnn::Mat& tensor, int expected_c, int expected_h, int expected_w)
{
    if(tensor.empty())
    {
        return false;
    }

    // NCNN张量属性：w=宽度, h=高度, c=通道数
    if(tensor.c != expected_c || tensor.h != expected_h || tensor.w != expected_w)
    {
        std::cerr << "[InputProcessor] Validation Failed! Expected: "
                  << expected_w << "x" << expected_h << "x" << expected_c
                  << ", Got: "
                  << tensor.w << "x" << tensor.h << "x" << tensor.c << std::endl;
        return false;
    }
    return true;
}

} // namespace Model
} // namespace DigitalHuman