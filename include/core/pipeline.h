#pragma once

#include <vector>
#include <opencv2/core.hpp>

namespace DigitalHuman {
namespace Core {

struct InferenceTask {
    double pts_ms = 0.0;
    std::vector<float> audio_feature;
    cv::Mat base_face;
};

} // namespace Core
} // namespace DigitalHuman