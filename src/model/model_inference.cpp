#include <iostream>
#include <chrono>
#include <ncnn/benchmark.h>

#include "model/model_inference.h"

namespace DigitalHuman 
{
namespace Model 
{

struct ModelInference::Impl
{
    ncnn::Net* net_ptr = nullptr;
    InferenceConfig config; // 创建配置结构体实例
    float last_latency = 0.0f;

    // 请求配置函数
    void applyConfig()
    {
        // 判断神经网络指针是否存在
        if(!net_ptr)
        {
            return;
        }

        net_ptr->opt.num_threads = config.num_threads;
        net_ptr->opt.use_vulkan_compute = config.use_vulkan;
        net_ptr->opt.use_fp16_packed = config.use_fp16;
        net_ptr->opt.use_fp16_storage = config.use_fp16;
        net_ptr->opt.use_fp16_arithmetic = config.use_fp16;
        net_ptr->opt.lightmode = config.light_mode;
    }

    int infer_internal(
        const ncnn::Mat& audio,
        const ncnn::Mat& face,
        ncnn::Mat& out)
    {
        // 先判断是否存在
        if(!net_ptr)
        {
            std::cerr << "[Inference] Error: Model not bound!" << std::endl;
            return -1;
        }

        // 判断数据是否有效
        if(audio.empty() || face.empty())
        {
            std::cerr << "[Inference] Error: Empty input tensor." << std::endl;
            return -1;
        }

        // 先应用配置，再创建extractor
        applyConfig();

        // 创建推理器
        ncnn::Extractor ex = net_ptr->create_extractor();
        ex.set_light_mode(config.light_mode); // 应用配置

        // 输入参数
        int ret = ex.input("mel", audio);
        if(ret != 0)
        {
            std::cerr << "[Inference] Error setting input 'mel' (code " << ret << ")" << std::endl;
            return -1;
        }
        ret = ex.input("face", face);
        if (ret != 0) {
            std::cerr << "[Inference] Error setting input 'face' (code " << ret << ")" << std::endl;
            return -1;
        }

        // 开始计时
        auto start = std::chrono::high_resolution_clock::now();

        // 开始推理，并输出到out中
        ret = ex.extract("pred", out);

        // 再次获取时间计算推理时间
        auto end = std::chrono::high_resolution_clock::now();
        last_latency = std::chrono::duration<float, std::milli>(end - start).count();

        if(ret != 0)
        {
            std::cerr << "[Inference] Error extracting output 'pred' (code " << ret << ")" << std::endl;
            return -1;
        }

        if(out.empty())
        {
            std::cerr << "[Inference] Error: Output tensor is empty!" << std::endl;
            return -1;
        }

        // 看接结果是否符合要求
        if(out.w != 96 || out.h != 96 || out.c != 3)
        {
            std::cerr << "[Inference] Warning: Unexpected output shape "
                      << out.w << "x" << out.h << "x" << out.c << std::endl;
        }

        return 0;
    }
};

ModelInference::ModelInference() : pImpl(std::make_unique<Impl>()) {}
ModelInference::~ModelInference() = default;
ModelInference::ModelInference(ModelInference&&) noexcept = default;
ModelInference& ModelInference::operator=(ModelInference&&) noexcept = default;

void ModelInference::bindModel(ncnn::Net* net)
{
    pImpl->net_ptr = net;
}

void ModelInference::setConfig(const InferenceConfig& config)
{
    pImpl->config = config;
    pImpl->applyConfig(); // 申请模型配置别忘了
}

int ModelInference::infer(const ncnn::Mat& audio_tensor,
                          const ncnn::Mat& face_tensor,
                          ncnn::Mat& out_tensor
)
{
    return pImpl->infer_internal(audio_tensor, face_tensor, out_tensor);
}

float ModelInference::getLastLatency() const
{
    return pImpl->last_latency;
}

}
}