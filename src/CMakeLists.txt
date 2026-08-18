# 自动搜集源文件
# 使用 GLOB_RECURSE 递归查找 src 下的所有子目录的 .cpp 文件
file(GLOB_RECURSE SRC_FILES
    "audio/*.cpp"
    "video/*.cpp"
    "core/*.cpp"
    "model/*.cpp"
    "utils/*.cpp"
)

# 生成 SDK 库
# 将所有源文件编译成一个名为 digital_human_core 的库
add_library(digital_human_core SHARED ${SRC_FILES})

# 配置库的头文件搜索路径
target_include_directories(digital_human_core PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

# 配置一些第三方库等
# 链接opencv库
target_link_libraries(digital_human_core PUBLIC
    ${OpenCV_LIBS}
    ncnn
    ${FFMPEG_LIBRARIES}
    ${PORTAUDIO_LIBRARIES}
    dlib::dlib
)

message("OpenCV libraries: ${OpenCV_LIBS}")

# 主程序
add_executable(digital_human_app main.cpp)

# 将主程序链接到 SDK 库
target_link_libraries(digital_human_app PRIVATE
    digital_human_core
)

# 实时数字人的目标
add_executable(digital_human_realtime_pulse_app main_realtime_pulse.cpp)

target_link_libraries(digital_human_realtime_pulse_app PRIVATE
    digital_human_core    
)