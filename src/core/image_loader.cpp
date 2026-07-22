#include <iostream>
#include <opencv2/imgcodecs.hpp>
#include <filesystem>

#include "core/image_loader.h"

// 使用命名空间简化
namespace fs = std::filesystem;

namespace DigitalHuman 
{
namespace Core 
{
    // Pimpl的内部实现类，里面包含了3种方法
    struct ImageLoader::Impl
    {
        cv::Mat loadFromFile(const std::string& filePath)
        {
            // 先检查文件是否存在
            if(!fs::exists(filePath))
            {
                // 抛出异常
                throw ImageLoaderException("File does not exist: " + filePath);
            }

            // 从文件路径加载图像
            // 默认为彩色 BGR
            cv::Mat image = cv::imread(filePath);
            if(image.empty())
            {
                throw ImageLoaderException("Failed to decode image: " + filePath);
            }
            else
            {
                return image;
            }
        }

        // 从内存加载
        cv::Mat loadFromMemory(const std::vector<unsigned char>& buffer)
        {
            // 先判断buffer是否为空
            if(buffer.empty())
            {
                throw ImageLoaderException("Buffer is empty");
            }

            // 如果不为空，使用cv::imdecode从内存解码
            cv::Mat image;
            try
            {
                image = cv::imdecode(buffer, cv::IMREAD_COLOR);
            }
            catch(const cv::Exception& e)
            {
                throw ImageLoaderException("Failed to decode image from memory: " + std::string(e.what()));
            }
            
            // 判断是否为空
            if(image.empty())
            {
                throw ImageLoaderException("Failed to decode image from memory");
            }

            return image;
        }

        // 批量加载图片
        std::vector<cv::Mat> loadBatch(const std::vector<std::string>& filePaths)
        {
            std::vector<cv::Mat> images; //保存返回值
            images.reserve(filePaths.size()); //内存预分配，防止频繁扩容消耗

            for(const auto& path : filePaths)
            {
                try
                {
                    // 直接用上面的文件读取
                    cv::Mat image = loadFromFile(path);
                    images.push_back(image);
                }
                catch(const ImageLoaderException& e)
                {
                    std::cerr << "\033[31m[Batch Load Error] " << e.what() << "\033[0m" << std::endl;\
                    // 这里要防止我其中一个读取失败我就直接抛出异常后面的都不读了
                    // 可选择存入一个空 Mat 保持索引对齐
                    // images.push_back(cv::Mat());
                    // 或者选择跳过当前文件
                    continue;
                }
            }
            return images;
        }
    };

    ImageLoader::ImageLoader() : pImpl(std::make_unique<Impl>()){}
    ImageLoader::~ImageLoader() = default;

    // 移动构造和赋值构造
    ImageLoader::ImageLoader(ImageLoader&&) noexcept = default;
    ImageLoader& ImageLoader::operator = (ImageLoader&&) noexcept = default;

    cv::Mat ImageLoader::loadFromFile(const std::string& filePath)
    {
        return pImpl->loadFromFile(filePath);
    }

    cv::Mat ImageLoader::loadFromMemory(const std::vector<unsigned char>& buffer)
    {
        return pImpl->loadFromMemory(buffer);
    }

    std::vector<cv::Mat> ImageLoader::loadBatch(const std::vector<std::string>& filepaths)
    {
        return pImpl->loadBatch(filepaths);
    }
} // namespace name
} // namespace DigitalHuman 
