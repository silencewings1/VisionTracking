#pragma once
#include "../def/macro_def.h"
#include "../def/type_def.h"

class Detector
{
public:
    Detector(const cv::Size& size);

    Corners process(const cv::Mat& image);
    void showResult(const cv::String& window_name, const Corners& corners, const cv::Mat& image);

private:
    /* detect in one image */
    std::tuple<CornersTemplate, bool> detectCorners(const cv::Mat& image);

    /* steps */
    cv::Mat convertToGray(const cv::Mat& image);
    std::tuple<cv::Mat, cv::Mat, cv::Mat> secondDerivCornerMetric();
    Maximas nonMaximumSuppression(const cv::Mat& img, int n = 8, int margin = 8, PixelType tau = 0.06f);
    std::tuple<CornersTemplate, bool> detectCornersOnMarker(const Maximas& corners);
    std::tuple<CornerTemplate, CornerTemplate, int> findFirstSecondCorners(const cv::Point& point);
    Corner subPixelLocation(const cv::Point& point);
    std::tuple<PixelType, PixelType> findEdgeAngles(const Corner& point);
    std::tuple<PixelType, PixelType> edgeOrientation(const cv::Mat& img_angle, const cv::Mat& img_weight);
    PixelType calcBolicCorrelation(const Corner& point, int width, PixelType theta);
    Corner findNextCorner(const CornerTemplate& current, int dir);
    CornerTemplate predictNextCorner(const CornerTemplate& current, int dir);

#ifdef USE_CUDA
    void initCuda(const cv::Size& size);
    std::tuple<cv::Mat, cv::Mat, cv::Mat> secondDerivCornerMetricCuda();
#endif

    /* others */
    void dump(const cv::String& name, const cv::Mat& mat);
    Eigen::MatrixXf calcPatchX();

private:
    const cv::Size SIZE;
    const int SIGMA;
    const int HALF_PATCH_SIZE;
    const Eigen::MatrixXf PATCH_X;
    const int WIDTH_MIN;
    const PixelType CORR_THRESHOLD;

    DetectRectangle rect;

    cv::Mat gray_image;
    cv::Mat I_angle;
    cv::Mat I_weight;
    cv::Mat cmax;

#ifdef USE_CUDA
    cv::Ptr<cv::cuda::Filter> filter_dx, filter_dy, filter_G;
    cv::cuda::GpuMat g_ones, g_zeros;
#endif
};