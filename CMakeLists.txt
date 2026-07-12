cmake_minimum_required(VERSION 3.16)
project(DigitalHumanSDK VERSION 0.1.0 LANGUAGES CXX)

############################### 全局配置
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 默认开启优化
add_compile_options(-O3)

# 启用本机指令集优化（如 AVX2 / SSE）
add_compile_options(-march=native)
add_definitions(-DNDEBUG)

# 常用警告
add_compile_options(-Wall -Wextra)

############################### OpenMP
find_package(OpenMP REQUIRED)
if(OpenMP_CXX_FOUND)
    add_compile_options(${OpenMP_CXX_FLAGS})
endif()

############################### 输出目录
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

############################### 全局头文件目录
include_directories(${CMAKE_SOURCE_DIR}/include)

############################### 第三方依赖

# 项目的第三方依赖

############################### 子目录
add_subdirectory(src)
add_subdirectory(examples)