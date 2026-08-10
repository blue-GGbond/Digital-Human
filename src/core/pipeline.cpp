#include "core/pipeline.h"
#include "utils/thread_safe_queue.h"
#include "audio/audio_video_Synchronous.h"
#include "core/frame_scheduler.h"
#include "video/video_frame.h"

#include "model/inference_process.h"
#include "video/render_process.h"

#include <ncnn/net.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>