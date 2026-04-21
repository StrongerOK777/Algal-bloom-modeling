#ifndef ALGAL_BLOOM_PAINTCOMPARE_H
#define ALGAL_BLOOM_PAINTCOMPARE_H

#include "test.h"

#include <array>
#include <cmath>
#include <filesystem>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#include "opencv2/imgcodecs.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/video/tracking.hpp"

static bool minMaxFinite(
    const std::vector<float>& data,
    float& minVal,
    float& maxVal,
    const std::vector<unsigned char>* validMask = nullptr
) {
    minVal = std::numeric_limits<float>::infinity();
    maxVal = -std::numeric_limits<float>::infinity();
    bool found = false;
    for (size_t i = 0; i < data.size(); ++i) {
        if (validMask && (*validMask)[i] == 0) continue;
        const float v = data[i];
        if (!std::isfinite(v)) continue;
        minVal = std::min(minVal, v);
        maxVal = std::max(maxVal, v);
        found = true;
    }
    if (!found) {
        minVal = 0.0f;
        maxVal = 1.0f;
        return false;
    }
    if (maxVal <= minVal) {
        maxVal = minVal + 1.0f;
    }
    return true;
}

static cv::Mat toGrayPreviewU8(const std::vector<float>& band, int width, int height) {
    float lo = 0.0f;
    float hi = 1.0f;
    minMaxFinite(band, lo, hi);
    const float scale = 255.0f / (hi - lo);

    cv::Mat gray(height, width, CV_8UC1, cv::Scalar(0));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
            const float v = band[idx];
            if (!std::isfinite(v)) {
                gray.at<unsigned char>(y, x) = 0;
                continue;
            }
            const float u = std::clamp((v - lo) * scale, 0.0f, 255.0f);
            gray.at<unsigned char>(y, x) = static_cast<unsigned char>(std::lround(u));
        }
    }
    return gray;
}

static std::vector<float> calcNdvi(const TifCube& cube) {
    const size_t pixelCount = static_cast<size_t>(cube.width) * static_cast<size_t>(cube.height);
    const size_t stride = pixelCount;
    const float* red = cube.data.data() + static_cast<size_t>(cube.bands - 2) * stride;
    const float* nir = cube.data.data() + static_cast<size_t>(cube.bands - 1) * stride;

    std::vector<float> ndvi(pixelCount, std::numeric_limits<float>::quiet_NaN());
    for (size_t i = 0; i < pixelCount; ++i) {
        const float r = red[i];
        const float n = nir[i];
        if (!std::isfinite(r) || !std::isfinite(n)) continue;
        const float denom = n + r;
        if (std::abs(denom) < 1e-8f) continue;
        ndvi[i] = (n - r) / denom;
    }
    return ndvi;
}

static cv::Mat buildNdviPseudoColor(const std::vector<float>& ndvi, int width, int height) {
    std::vector<float> clipped(ndvi);
    for (float& v : clipped) {
        if (!std::isfinite(v)) v = -0.2f;
        v = std::clamp(v, -0.2f, 0.6f);
    }

    cv::Mat u8(height, width, CV_8UC1, cv::Scalar(0));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
            const float t = (clipped[idx] + 0.2f) / 0.8f;
            u8.at<unsigned char>(y, x) = static_cast<unsigned char>(std::lround(std::clamp(t, 0.0f, 1.0f) * 255.0f));
        }
    }

    cv::Mat color;
    cv::applyColorMap(u8, color, cv::COLORMAP_TURBO);
    return color;
}

static cv::Mat buildBloomMap(const std::vector<float>& ndvi, int width, int height) {
    const cv::Vec3b water(255, 179, 128);  // BGR, 浅蓝
    const cv::Vec3b bloom(0, 128, 0);      // BGR, 绿色
    cv::Mat out(height, width, CV_8UC3, water);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
            const float v = ndvi[idx];
            if (std::isfinite(v) && v >= 0.0f) {
                out.at<cv::Vec3b>(y, x) = bloom;
            }
        }
    }
    return out;
}

static void formatNdviPairForFlow(
    const std::vector<float>& ndvi0,
    const std::vector<float>& ndvi1,
    int width,
    int height,
    cv::Mat& prevU8,
    cv::Mat& currU8,
    std::vector<unsigned char>& commonValid
) {
    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    commonValid.assign(pixelCount, 0);
    prevU8 = cv::Mat(height, width, CV_8UC1, cv::Scalar(0));
    currU8 = cv::Mat(height, width, CV_8UC1, cv::Scalar(0));

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
            const bool valid = std::isfinite(ndvi0[idx]) && std::isfinite(ndvi1[idx]);
            commonValid[idx] = valid ? 1 : 0;

            float v0 = valid ? ndvi0[idx] : -0.2f;
            float v1 = valid ? ndvi1[idx] : -0.2f;
            v0 = std::clamp(v0, -0.2f, 0.6f);
            v1 = std::clamp(v1, -0.2f, 0.6f);

            const float n0 = (v0 + 0.2f) / 0.8f;
            const float n1 = (v1 + 0.2f) / 0.8f;
            prevU8.at<unsigned char>(y, x) = static_cast<unsigned char>(std::lround(std::clamp(n0, 0.0f, 1.0f) * 255.0f));
            currU8.at<unsigned char>(y, x) = static_cast<unsigned char>(std::lround(std::clamp(n1, 0.0f, 1.0f) * 255.0f));
        }
    }
}

static cv::Mat buildVectorOverlay(
    const std::vector<float>& ndvi0,
    int width,
    int height,
    const cv::Mat& flow,
    const std::vector<unsigned char>& commonValid
) {
    cv::Mat canvas = buildBloomMap(ndvi0, width, height);

    const float scaleToVelocity = 50.0f / 2141.0f;
    const float visScale = 200.0f;
    const int step = 30;

    for (int y = 0; y < height; y += step) {
        for (int x = 0; x < width; x += step) {
            const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
            if (commonValid[idx] == 0) continue;

            const cv::Vec2f f = flow.at<cv::Vec2f>(y, x);
            const float vx = f[0] * scaleToVelocity;
            const float vy = -f[1] * scaleToVelocity;

            if (!std::isfinite(vx) || !std::isfinite(vy)) continue;
            const float mag = std::sqrt(vx * vx + vy * vy);
            if (mag < 0.02f) continue;

            const cv::Point p0(x, y);
            const cv::Point p1(
                static_cast<int>(std::lround(static_cast<float>(x) + vx * visScale)),
                static_cast<int>(std::lround(static_cast<float>(y) - vy * visScale))
            );

            cv::arrowedLine(canvas, p0, p1, cv::Scalar(0, 0, 255), 2, cv::LINE_AA, 0, 0.35);
        }
    }

    return canvas;
}

static int runPaintCompare(
    const std::filesystem::path& outputDir = "./output",
    const std::string& gf1Path = "../Example/GeoTIFF_Landmasked/2021_05_30_10_38_06_GF1.tif",
    const std::string& gf4Path = "../Example/GeoTIFF_Landmasked/2021_05_30_11_13_47_GF4.tif"
) {
    namespace fs = std::filesystem;

    GDALAllRegister();
    fs::create_directories(outputDir);

    TifCube cube0;
    TifCube cube1;
    if (!tifReader(gf1Path, cube0)) return 1;
    if (!tifReader(gf4Path, cube1)) return 1;
    if (cube0.bands < 4 || cube1.bands < 4) return 1;
    if (cube0.width != cube1.width || cube0.height != cube1.height) return 1;

    const int width = cube0.width;
    const int height = cube0.height;
    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);

    const float* nir0 = cube0.data.data() + static_cast<size_t>(cube0.bands - 1) * pixelCount;
    const float* nir1 = cube1.data.data() + static_cast<size_t>(cube1.bands - 1) * pixelCount;
    std::vector<float> nirVec0(nir0, nir0 + pixelCount);
    std::vector<float> nirVec1(nir1, nir1 + pixelCount);

    const fs::path infrared1Path = outputDir / "infrared1.png";
    const fs::path infrared2Path = outputDir / "infrared2.png";
    if (!cv::imwrite(infrared1Path.string(), toGrayPreviewU8(nirVec0, width, height))) return 1;
    if (!cv::imwrite(infrared2Path.string(), toGrayPreviewU8(nirVec1, width, height))) return 1;

    const std::vector<float> ndvi0 = calcNdvi(cube0);
    const std::vector<float> ndvi1 = calcNdvi(cube1);

    const fs::path ndvi1Path = outputDir / "ndvi1.png";
    const fs::path ndvi2Path = outputDir / "ndvi2.png";
    if (!cv::imwrite(ndvi1Path.string(), buildNdviPseudoColor(ndvi0, width, height))) return 1;
    if (!cv::imwrite(ndvi2Path.string(), buildNdviPseudoColor(ndvi1, width, height))) return 1;

    const fs::path bloom1Path = outputDir / "bloom1.png";
    const fs::path bloom2Path = outputDir / "bloom2.png";
    if (!cv::imwrite(bloom1Path.string(), buildBloomMap(ndvi0, width, height))) return 1;
    if (!cv::imwrite(bloom2Path.string(), buildBloomMap(ndvi1, width, height))) return 1;

    cv::Mat prevU8;
    cv::Mat currU8;
    std::vector<unsigned char> commonValid;
    formatNdviPairForFlow(ndvi0, ndvi1, width, height, prevU8, currU8, commonValid);

    cv::Mat flowInit(height, width, CV_32FC2, cv::Scalar(0.0f, 0.0f));
    cv::Mat flow;
    cv::calcOpticalFlowFarneback(
        prevU8,
        currU8,
        flow,
        0.5,
        5,
        80,
        10,
        7,
        1.5,
        cv::OPTFLOW_FARNEBACK_GAUSSIAN
    );

    const fs::path vectorPath = outputDir / "vector.png";
    if (!cv::imwrite(vectorPath.string(), buildVectorOverlay(ndvi0, width, height, flow, commonValid))) return 1;

    return 0;
}

#endif
