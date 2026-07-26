#include <iostream>
#include <vector>
#include <opencv2/opencv.hpp>

#include "core/face_mask_generator.h"

namespace DigitalHuman 
{
namespace Core
{

struct FaceMaskGenerator::Impl
{
    // 实现内部函数
    cv::Mat generateMouthMask(const cv::Size& image_size,
                              const std::vector<cv::Point>& landmarks,
                              int dilate_radius,
                              int blur_ksize) 
    {
        // 1. 先初始化全黑画布 CV_8UC1
        cv::Mat mask = cv::Mat::zeros(image_size, CV_8UC1);

        // 检查关键个数
        if(landmarks.size() != 68)
        {
            std::cerr << "[FaceMaskGenerator] Error : Invalid landmarks count." << std::endl;
            return mask;
        }

        // 2. 提取嘴唇外轮廓 关键点 48-59
        std::vector<cv::Point> mouth_points;
        for(int i = 48; i < 60; ++i)
        {
            mouth_points.push_back(landmarks[i]);
        }

        // 3. 绘制挖空的区域，必须是指针的指针，因为 fillPoly 支持一次画多个多边形
        const cv::Point* ppt[1] = {mouth_points.data()}; // ppt[0]存储了嘴巴的轮廓点
        int npt[] = {static_cast<int>(mouth_points.size())}; // npt[0]存储了嘴巴的轮廓点数量
        cv::fillPoly(mask, ppt, npt, 1, cv::Scalar(255)); // 填充嘴巴区域为白色

        // 4. 扩展区域，这里扩展嘴部区域
        if(dilate_radius > 0)
        {
            // 先获取卷积核
            int k_size = dilate_radius * 2 + 1;
            cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(k_size, k_size));
            cv::dilate(mask, mask, kernel);
        }

        // 5. 边缘羽化，这里使用高斯模糊
        if(blur_ksize > 0)
        {
            // 检查是不是奇数，不是则加 1
            if(blur_ksize % 2 == 0)
            {
                blur_ksize++;
            }
            cv::GaussianBlur(mask, mask, cv::Size(blur_ksize, blur_ksize), 0);
        }

        // 6. 归一化并转为浮点(0-255 -> 0.0-1.0)
        cv::Mat mask_float;
        mask.convertTo(mask_float, CV_32FC1, 1.0 / 255.0);

        return mask_float;
    }
};

FaceMaskGenerator::FaceMaskGenerator() : pimpl(std::make_unique<Impl>()) {}
FaceMaskGenerator::~FaceMaskGenerator() = default;

FaceMaskGenerator::FaceMaskGenerator(FaceMaskGenerator&&) noexcept = default;
FaceMaskGenerator& FaceMaskGenerator::operator=(FaceMaskGenerator&&) noexcept = default;

// 外部调用接口
cv::Mat FaceMaskGenerator::generateMouthMask(const cv::Size& image_size,
                                              const std::vector<cv::Point>& landmarks,
                                              int dilate_radius,
                                              int blur_ksize) 
{
    return pimpl->generateMouthMask(image_size, landmarks, dilate_radius, blur_ksize);
}

}// namespace Core
}// namespace DigitalHuman
