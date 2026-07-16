# 构建 Demo App（基础演示）
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/demo.cpp")
    add_executable(demo_app demo.cpp)
    # 链接 SDK 核心库
    target_link_libraries(demo_app PRIVATE digital_human_core)
endif()

# 2. 构建 OpenCV 测试程序（验证图像功能）
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/opencv_test.cpp")
    add_executable(opencv_test opencv_test.cpp)

    # 链接 SDK 核心库
    # 因为 digital_human_core 在 src 中使用了 PUBLIC 链接 OpenCV，
    # 所以这里会自动获得 OpenCV 的头文件路径和库文件，无需重复 Link OpenCV
    target_link_libraries(opencv_test PRIVATE digital_human_core)
endif()

message(STATUS "Configured example targets.")

# 3. 构建 ncnn 推理测试
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ncnn_test.cpp")
    add_executable(ncnn_test ncnn_test.cpp)

    # 链接 SDK 核心库
    target_link_libraries(ncnn_test PRIVATE digital_human_core)

endif()
