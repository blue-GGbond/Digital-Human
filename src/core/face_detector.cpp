#include <iostream>

#include <opencv2/imgproc.hpp>
#include <opencv2/core.hpp>
#include <dlib/opencv.h>
#include <dlib/image_processing.h>
#include <dlib/image_processing/frontal_face_detector.h>

#include "core/face_detector.h"

namespace DigitalHuman 
{
namespace Core
{

// Impl
struct FaceDetector::Impl
{
    // dlib的人脸检测器
    dlib::frontal_face_detector  detector;

    // 形状检测器
    dlib::shape_predictor landmarks_predictor;
    bool is_model_loaded = false;

    Impl()
    {
        // 初始化检测器
        detector = dlib::get_frontal_face_detector();
    }

    // 加载模型
    bool loadLandmarkModel(const std::string& modelPath)
    {
        try
        {
            dlib::deserialize(modelPath) >> landmarks_predictor;
            is_model_loaded = true;
            return true;
        }
        catch(const dlib::serialization_error& e)
        {
            std::cerr << "[FaceDetector] Error: Failed to load landmark model from " << modelPath << ". Error: " << e.what() << std::endl;
            is_model_loaded = false;
            return false;
        }
    }

    // 内部函数实现
    std::vector<cv::Rect> detect(const cv::Mat& image)
    {
        std::vector<cv::Rect> results; // 存储检测到的人脸边界框

        if(image.empty())
        {
            std::cerr << "[FaceDetector] Warning: Input image is empty." <<std::endl;
        }

        // 做采样策略
        cv::Mat process_img;
        float scale = 1.0f;
        const int MAX_WIDTH = 1600; // 限制最大宽度为1600px
        const int MIN_WIDTH = 800; // 低清图上采样阈值

        // 降低采样处理过大的图片
        if(image.cols > MAX_WIDTH)
        {
            scale = (float)MAX_WIDTH / image.cols;
            cv::resize(image, process_img, cv::Size(), scale, scale);
        }
        else if(image.cols < MIN_WIDTH)
        {
            // 图片过小放大采样率
            scale = (float)MIN_WIDTH / image.cols;
            cv::resize(image, process_img, cv::Size(), scale, scale);
        }
        else
        {
            // 刚刚好
            process_img = image;
        }

        // 将OpenCV图像包装为dlib图像
        try
        {
            dlib::cv_image<dlib::rgb_pixel> dlib_img(process_img);

            // 执行测试
            std::vector<dlib::rectangle> dets = detector(dlib_img, 0);

            // 如果放大后还没检测到尝试用内部 Pyramid Up 
            if(dets.empty() && process_img.cols < 1200)
            {
                dets = detector(dlib_img, 1);
            }

            std::cout<< "[FaceDetector] Raw dlib detections: " << dets.size() << std::endl;

            results.reserve(dets.size()); // 预分配内存
            for(const auto& d : dets)
            {
                // 将坐标映射回原始图像坐标
                int x = static_cast<int>(d.left() / scale);
                int y = static_cast<int>(d.top() / scale);
                int w = static_cast<int>(d.width() / scale);
                int h = static_cast<int>(d.height() / scale);
                
                // 边界保护（如果人脸边界超出图像范围，调整为图像边界）
                x = std::max(x, 0);
                y = std::max(y, 0);
                w = std::min(w, image.cols - x);
                h = std::min(h, image.rows - y);

                if(w > 0 && h > 0)
                {
                    results.push_back(cv::Rect(x, y, w, h));
                }
            }
        }
        catch(const std::exception& e)
        {
            std::cerr << "[FaceDetector] Error: " << e.what() << std::endl;
        }
        
        return results;
    }

    // 获取人脸关键点 (68点)
    std::vector<cv::Point> getLandmarks(const cv::Mat& image, const cv::Rect& faceRect)
    {
        std::vector<cv::Point> landmarks; // 存储68个点的坐标

        // 检测模型是否加载
        if(!is_model_loaded || image.empty())
        {
            return landmarks;
        }
        if(faceRect.width < 10 || faceRect.height < 10)
        {
            return landmarks;
        }

        try
        {
            // 这里使用原始分辨率的图像来获得最高的关键点精度
            // dlib::cv_image 是零拷贝的，所以大图也不会有内存开销，只是计算慢一点
            dlib::cv_image<dlib::rgb_pixel> dlib_img(image);

            // 转换为dlib::rectangle
            dlib::rectangle dlib_faceRect
            (
                faceRect.x,
                faceRect.y,
                faceRect.x + faceRect.width - 1,
                faceRect.y + faceRect.height - 1
            );

            // 预测
            dlib::full_object_detection shape = landmarks_predictor(dlib_img, dlib_faceRect);

            // 只有当预测到68个点时才有效
            if(shape.num_parts() == 68)
            {
                landmarks.reserve(68);
                for(unsigned int i = 0; i < 68; ++i)
                {
                    landmarks.push_back(cv::Point(shape.part(i).x(), shape.part(i).y()));
                }
            }
        }
        catch(const std::exception& e)
        {
            std::cerr << "[FaceDetector] Landmark Error: " << e.what() << std::endl;
        }

        return landmarks;
    }
};

FaceDetector::FaceDetector() : pImpl(std::make_unique<Impl>())
{}
FaceDetector::~FaceDetector() = default;

std::vector<cv::Rect> FaceDetector::detect(const cv::Mat& image)
{
    return pImpl->detect(image);
}

bool FaceDetector::loadLandmarkModel(const std::string& modelPath)
{
    return pImpl->loadLandmarkModel(modelPath);
}

std::vector<cv::Point> FaceDetector::getLandmarks(const cv::Mat& image, const cv::Rect& faceRect)
{
    return pImpl->getLandmarks(image, faceRect);
}

}// namespace Core
}// namespace DigitalHuman