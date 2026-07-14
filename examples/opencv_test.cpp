#include <iostream>
#include <string>
#include <vector>
#include <opencv2/opencv.hpp>

/**
 * @brief 绘制一个简单的 C++ 房子图案
 * 
 * 本函数演示了 OpenCV 的基本绘图功能：
 * - cv::line(): 绘制直线
 * - cv::rectangle(): 绘制矩形（可填充）
 * - cv::fillPoly(): 填充多边形
 * - cv::putText(): 绘制文字
 * 
 * @param img 目标图像，类型为 cv::Mat（OpenCV 的核心矩阵/图像类）
 *            cv::Mat 是 OpenCV 中用于存储图像、矩阵等多维数据的核心类
 */
void drawCppHouse(cv::Mat& img) {
    // ============================================================
    // OpenCV 颜色格式说明：
    // OpenCV 默认使用 BGR 颜色空间（不是 RGB！）
    // cv::Scalar(B, G, R) - 蓝色、绿色、红色三个通道的值，范围 0-255
    // ============================================================

    // --------------------------------------------------------
    // 1. 绘制地面 - cv::line()
    // --------------------------------------------------------
    // 函数原型：void line(Mat& img, Point pt1, Point pt2, const Scalar& color, int thickness=1)
    // 参数说明：
    //   - img: 要绘制的目标图像
    //   - pt1, pt2: 直线的起点和终点坐标 cv::Point(x, y)
    //   - color: 线条颜色，cv::Scalar(50, 200, 50) = 绿色（B=50, G=200, R=50）
    //   - thickness: 线条粗细，单位是像素
    cv::line(img, cv::Point(0, 550), cv::Point(600, 550), cv::Scalar(50, 200, 50), 5);

    // --------------------------------------------------------
    // 2. 绘制房子主体 - cv::rectangle()
    // --------------------------------------------------------
    // 函数原型：void rectangle(Mat& img, Rect rec, const Scalar& color, int thickness=1)
    // 参数说明：
    //   - img: 目标图像
    //   - rec: cv::Rect 矩形，参数为 (x, y, width, height)
    //          x=150, y=300 是矩形左上角的坐标
    //          width=300, height=250 是矩形的宽高
    //   - color: 填充颜色，cv::Scalar(100, 100, 100) = 灰色
    //   - thickness: 线条粗细，-1 表示填充整个矩形
    cv::Rect house_body(150, 300, 300, 250);
    cv::rectangle(img, house_body, cv::Scalar(100, 100, 100), -1);

    // --------------------------------------------------------
    // 3. 绘制屋顶 - cv::fillPoly()
    // --------------------------------------------------------
    // 函数原型：void fillPoly(Mat& img, const Point** pts, const int* npts, int ncontours, const Scalar& color)
    // 简化版本：void fillPoly(Mat& img, InputArrayOfArrays pts, const Scalar& color)
    // 
    // 参数说明：
    //   - img: 目标图像
    //   - pts: 多边形顶点数组，使用 std::vector<cv::Point> 存储
    //   - color: 填充颜色，cv::Scalar(50, 50, 200) = 红色（B=50, G=50, R=200）
    //
    // cv::Point 是 OpenCV 中表示二维坐标点的类，包含 x 和 y 两个成员
    std::vector<cv::Point> roof_points;
    roof_points.push_back(cv::Point(130, 300));  // 屋顶左下角
    roof_points.push_back(cv::Point(470, 300));  // 屋顶右下角
    roof_points.push_back(cv::Point(300, 150));  // 屋顶顶点
    cv::fillPoly(img, roof_points, cv::Scalar(50, 50, 200));

    // --------------------------------------------------------
    // 4. 绘制文字 "C++" - cv::putText()
    // --------------------------------------------------------
    // 函数原型：void putText(Mat& img, const String& text, Point org, int fontFace, double fontScale, Scalar color, int thickness=1)
    // 参数说明：
    //   - img: 目标图像
    //   - text: 要绘制的文字字符串
    //   - org: 文字左下角的坐标位置
    //   - fontFace: 字体类型，cv::FONT_HERSHEY_DUPLEX 是一种无衬线字体
    //   - fontScale: 字体缩放比例，数值越大字越大
    //   - color: 文字颜色，cv::Scalar(255, 255, 255) = 白色
    //   - thickness: 笔画粗细
    cv::putText(img, "C", cv::Point(275, 280), cv::FONT_HERSHEY_DUPLEX, 3.0, cv::Scalar(255, 255, 255), 4);
    cv::putText(img, "+", cv::Point(200, 450), cv::FONT_HERSHEY_DUPLEX, 4.0, cv::Scalar(0, 255, 255), 5);
    cv::putText(img, "+", cv::Point(340, 450), cv::FONT_HERSHEY_DUPLEX, 4.0, cv::Scalar(0, 255, 255), 5);
}

/**
 * @brief 主函数：演示 OpenCV 图像读写、处理和保存的完整流程
 * 
 * 本程序展示了 OpenCV 的核心功能：
 * 1. 创建空白图像
 * 2. 在图像上绘制图形
 * 3. 保存图像到文件
 * 4. 从文件读取图像
 * 5. 图像颜色空间转换
 * 6. 保存处理后的图像
 */
int main() {
    std::cout << "\n==========================================" << std::endl;
    std::cout << "   Digital Human SDK: OpenCV Test" << std::endl;
    std::cout << "==========================================\n" << std::endl;

    // ============================================================
    // 步骤 1: 验证 OpenCV 安装并输出版本信息
    // ============================================================
    // CV_VERSION 是 OpenCV 定义的宏，表示当前安装的版本号
    // 例如 "4.5.4" 表示 OpenCV 4.5.4 版本
    std::cout << "[1] Checking OpenCV Installation..." << std::endl;
    std::cout << "   -> OpenCV Version: " << CV_VERSION << std::endl;

    // ============================================================
    // 步骤 2: 创建图像并绘制内容
    // ============================================================
    std::cout << "[2] Generating test image in memory..." << std::endl;
    
    // --------------------------------------------------------
    // cv::Mat 创建图像
    // --------------------------------------------------------
    // cv::Mat::zeros(rows, cols, type) 创建一个全零（黑色）的图像
    // 参数说明：
    //   - rows: 图像高度（行数），这里是 600 像素
    //   - cols: 图像宽度（列数），这里是 600 像素
    //   - type: 图像数据类型，CV_8UC3 表示：
    //           - 8U: 8位无符号整数（0-255）
    //           - C3: 3个通道（BGR彩色图像）
    //
    // 其他常用类型：
    //   - CV_8UC1: 8位单通道灰度图
    //   - CV_32FC3: 32位浮点3通道
    cv::Mat source_img = cv::Mat::zeros(600, 600, CV_8UC3);
    
    // 调用绘图函数在图像上绘制房子
    drawCppHouse(source_img);
    
    // ============================================================
    // 步骤 3: 保存图像到文件 - cv::imwrite()
    // ============================================================
    // 函数原型：bool imwrite(const String& filename, InputArray img)
    // 参数说明：
    //   - filename: 保存的文件名（包含扩展名）
    //               扩展名决定格式：.jpg .png .bmp .tiff 等
    //   - img: 要保存的 cv::Mat 图像
    // 返回值：成功返回 true，失败返回 false
    //
    // 注意：OpenCV 会根据文件扩展名自动选择编码格式
    std::string src_filename = "test_src.jpg";
    if (cv::imwrite(src_filename, source_img)) {
        std::cout << "   -> [SUCCESS] Generated and saved: " << src_filename << std::endl;
    } else {
        std::cerr << "   -> [FAILED] Could not save initial image!" << std::endl;
        return -1;
    }

    // ============================================================
    // 步骤 4: 从文件读取图像 - cv::imread()
    // ============================================================
    std::cout << "[3] Testing imread (Loading image from disk)..." << std::endl;
    
    // --------------------------------------------------------
    // cv::imread() 读取图像
    // --------------------------------------------------------
    // 函数原型：Mat imread(const String& filename, int flags = IMREAD_COLOR)
    // 参数说明：
    //   - filename: 要读取的图像文件路径
    //   - flags: 读取模式（可选），常用值：
    //            - cv::IMREAD_COLOR (1): 以彩色模式读取（默认），忽略透明度
    //            - cv::IMREAD_GRAYSCALE (0): 以灰度模式读取
    //            - cv::IMREAD_UNCHANGED (-1): 按原样读取，包括 alpha 通道
    // 返回值：cv::Mat 对象，如果读取失败则返回空矩阵（empty() 返回 true）
    cv::Mat loaded_img = cv::imread(src_filename);

    // 检查图像是否成功读取
    // cv::Mat::empty() 返回 true 表示矩阵为空（读取失败）
    if (loaded_img.empty()) {
        std::cerr << "   -> [FAILED] Could not open or find the image: " << src_filename << std::endl;
        return -1;
    }
    
    // --------------------------------------------------------
    // 获取图像属性
    // --------------------------------------------------------
    // cv::Mat 的重要成员：
    //   - cols: 图像宽度（列数）
    //   - rows: 图像高度（行数）
    //   - channels(): 返回通道数（1=灰度，3=彩色BGR，4=彩色BGRA）
    std::cout << "   -> [SUCCESS] Image loaded." << std::endl;
    std::cout << "   -> Resolution: " << loaded_img.cols << "x" << loaded_img.rows << std::endl;
    std::cout << "   -> Channels: " << loaded_img.channels() << std::endl;

    // ============================================================
    // 步骤 5: 颜色空间转换 - cv::cvtColor()
    // ============================================================
    std::cout << "[4] Testing color space conversion (BGR -> Grayscale)..." << std::endl;
    
    // 声明输出图像，此时还是空的
    cv::Mat gray_img;
    
    // --------------------------------------------------------
    // cv::cvtColor() 颜色空间转换
    // --------------------------------------------------------
    // 函数原型：void cvtColor(InputArray src, OutputArray dst, int code, int dstCn = 0)
    // 参数说明：
    //   - src: 源图像（输入）
    //   - dst: 目标图像（输出），会自动分配内存
    //   - code: 转换类型代码，常用值：
    //           - cv::COLOR_BGR2GRAY: BGR彩色转灰度
    //           - cv::COLOR_BGR2RGB: BGR转RGB
    //           - cv::COLOR_BGR2HSV: BGR转HSV色彩空间
    //           - cv::COLOR_GRAY2BGR: 灰度转BGR彩色
    //   - dstCn: 目标图像的通道数，0表示自动
    //
    // 注意：OpenCV 默认使用 BGR 格式，不是 RGB！
    try {
        cv::cvtColor(loaded_img, gray_img, cv::COLOR_BGR2GRAY);
        std::cout << "   -> [SUCCESS] Converted to Grayscale." << std::endl;
        std::cout << "   -> New Channels: " << gray_img.channels() << std::endl;
    } catch (const cv::Exception& e) {
        // OpenCV 的错误处理：所有 OpenCV 错误都会抛出 cv::Exception
        std::cerr << "   -> [FAILED] OpenCV Exception: " << e.what() << std::endl;
        return -1;
    }

    // ============================================================
    // 步骤 6: 保存处理后的图像
    // ============================================================
    std::cout << "[5] Saving processed image..." << std::endl;
    std::string dst_filename = "test_result_gray.jpg";
    if (cv::imwrite(dst_filename, gray_img)) {
        std::cout << "   -> [SUCCESS] Saved grayscale image to: " << dst_filename << std::endl;
    } else {
        std::cerr << "   -> [FAILED] Could not save grayscale image!" << std::endl;
        return -1;
    }

    // ============================================================
    // 程序结束
    // ============================================================
    std::cout << "\n==========================================" << std::endl;
    std::cout << "   ALL TESTS PASSED SUCCESSFULLY" << std::endl;
    std::cout << "==========================================\n" << std::endl;

    return 0;
}