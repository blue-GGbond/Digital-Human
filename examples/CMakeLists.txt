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

# 4. 构建 FFmpeg 测试程序 (验证音频功能)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg_audio_test.cpp")
    add_executable(ffmpeg_audio_test ffmpeg_audio_test.cpp)

    # 链接 SDK 核心库
    target_link_libraries(ffmpeg_audio_test PRIVATE digital_human_core)
endif()

# 5. 构建 dlib 测试程序 (验证人脸检测和关键点提取)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/dlib_test.cpp")
    add_executable(dlib_test dlib_test.cpp)

    # 链接 SDK 核心库
    target_link_libraries(dlib_test PRIVATE digital_human_core)
endif()

# 6. ImageLoader Test 
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/image_loader_test.cpp")
    add_executable(image_loader_test image_loader_test.cpp)
    target_link_libraries(image_loader_test PRIVATE digital_human_core)
endif()

# 7. FaceDetector
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/face_detector_test.cpp") 
    add_executable(face_detector_test face_detector_test.cpp)

    target_link_libraries(face_detector_test PRIVATE digital_human_core)
endif()

# 8. landmarks
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/landmark_test.cpp") 
    add_executable(landmark_test landmark_test.cpp)

    target_link_libraries(landmark_test PRIVATE digital_human_core)
endif()

# 9. face_aligner
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/face_aligner_test.cpp") 
    add_executable(face_aligner_test face_aligner_test.cpp)

    target_link_libraries(face_aligner_test PRIVATE digital_human_core)
endif()

# 10. mask_test
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/face_mask_test.cpp") 
    add_executable(face_mask_test face_mask_test.cpp)

    target_link_libraries(face_mask_test PRIVATE digital_human_core)
endif()

# 11. audio_loader_test
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/audio_loader_test.cpp") 
    add_executable(audio_loader_test audio_loader_test.cpp)

    target_link_libraries(audio_loader_test PRIVATE digital_human_core)
endif()

# 12. audio_framer_test
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/audio_framer_test.cpp") 
    add_executable(audio_framer_test audio_framer_test.cpp)

    target_link_libraries(audio_framer_test PRIVATE digital_human_core)
endif()

# 13. audio_mel_feature_extract_test
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/audio_mel_feature_extract_test.cpp") 
    add_executable(audio_mel_feature_extract_test audio_mel_feature_extract_test.cpp)

    target_link_libraries(audio_mel_feature_extract_test PRIVATE digital_human_core)
endif()

# 14. audio_preprocess_test
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/audio_preprocess_test.cpp") 
    add_executable(audio_preprocess_test audio_preprocess_test.cpp)

    target_link_libraries(audio_preprocess_test PRIVATE digital_human_core)
endif()