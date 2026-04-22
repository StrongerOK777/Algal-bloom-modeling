#ifndef ALGAL_BLOOM_PAINTCOMPARE_H
#define ALGAL_BLOOM_PAINTCOMPARE_H

#include "test.h"
#include "Earlywarning.h"

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

static std::vector<float> buildBloomBinaryFromNdvi(const std::vector<float>& ndvi) {
    std::vector<float> out(ndvi.size(), 0.0f);
    for (size_t i = 0; i < ndvi.size(); ++i) {
        const float v = ndvi[i];
        if (!std::isfinite(v)) {
            out[i] = std::numeric_limits<float>::quiet_NaN();
            continue;
        }
        out[i] = (v >= 0.0f) ? 1.0f : 0.0f;
    }
    return out;
}

static cv::Mat buildBloomMapFromBinary(const std::vector<float>& bloom, int width, int height) {
    const cv::Vec3b water(255, 179, 128);  // BGR, 浅蓝
    const cv::Vec3b algae(0, 128, 0);      // BGR, 绿色
    cv::Mat out(height, width, CV_8UC3, water);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
            const float v = bloom[idx];
            if (std::isfinite(v) && v >= 0.5f) {
                out.at<cv::Vec3b>(y, x) = algae;
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

static std::vector<float> moveBloomPixels(
    const std::vector<float>& bloomPixels,
    int width,
    int height,
    const std::vector<cv::Vec2f>& offset
) {
    cv::Mat movedMask(height, width, CV_8UC1, cv::Scalar(0));

    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            const size_t idx = static_cast<size_t>(row) * static_cast<size_t>(width) + static_cast<size_t>(col);
            const float v = bloomPixels[idx];
            if (!std::isfinite(v)) continue;
            if (v < 0.5f) continue;

            const cv::Vec2f d = offset[idx];
            const int rowNew = std::clamp(
                static_cast<int>(std::lround(static_cast<float>(row) - d[1])),
                0,
                height - 1
            );
            const int colNew = std::clamp(
                static_cast<int>(std::lround(static_cast<float>(col) + d[0])),
                0,
                width - 1
            );

            // 将起点到终点的位移路径一并填充，减小离散迁移导致的割裂断层。
            cv::line(
                movedMask,
                cv::Point(col, row),
                cv::Point(colNew, rowNew),
                cv::Scalar(255),
                1,
                cv::LINE_8
            );
        }
    }

    // 小尺度闭运算，连接细小断缝但不过度扩张边界。
    const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3));
    cv::morphologyEx(movedMask, movedMask, cv::MORPH_CLOSE, kernel, cv::Point(-1, -1), 1);

    std::vector<float> moved(static_cast<size_t>(width) * static_cast<size_t>(height), 0.0f);
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            const size_t idx = static_cast<size_t>(row) * static_cast<size_t>(width) + static_cast<size_t>(col);
            moved[idx] = (movedMask.at<unsigned char>(row, col) > 0) ? 1.0f : 0.0f;
        }
    }

    for (size_t i = 0; i < bloomPixels.size(); ++i) {
        if (!std::isfinite(bloomPixels[i])) {
            moved[i] = std::numeric_limits<float>::quiet_NaN();
        }
    }
    return moved;
}

static void addPanelTitle(cv::Mat& img, const std::string& title) {
    cv::putText(
        img,
        title,
        cv::Point(20, 38),
        cv::FONT_HERSHEY_SIMPLEX,
        1.0,
        cv::Scalar(0, 0, 0),
        2,
        cv::LINE_AA
    );
}

static cv::Mat buildForecastGrid(const std::vector<cv::Mat>& frames) {
    if (frames.empty()) return cv::Mat();

    const int cols = 4;
    const int rows = static_cast<int>((frames.size() + static_cast<size_t>(cols) - 1) / static_cast<size_t>(cols));
    const int h = frames[0].rows;
    const int w = frames[0].cols;

    cv::Mat grid(h * rows, w * cols, frames[0].type(), cv::Scalar(255, 179, 128));
    for (size_t i = 0; i < frames.size(); ++i) {
        const int r = static_cast<int>(i) / cols;
        const int c = static_cast<int>(i) % cols;
        cv::Rect roi(c * w, r * h, w, h);
        cv::Mat panel = frames[i].clone();
        addPanelTitle(panel, std::to_string(i + 1) + " hour later");
        panel.copyTo(grid(roi));
    }
    return grid;
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

    const std::vector<float> bloomTime1Binary = buildBloomBinaryFromNdvi(ndvi1);
    const fs::path vectorBloomPath = outputDir / "vector_bloom.png";
    if (!cv::imwrite(vectorBloomPath.string(), buildVectorOverlay(bloomTime1Binary, width, height, flow, commonValid))) return 1;

    const float offsetScale = 3600.0f / 2141.0f;
    std::vector<cv::Vec2f> baseOffset(pixelCount, cv::Vec2f(0.0f, 0.0f));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
            const cv::Vec2f f = flow.at<cv::Vec2f>(y, x);
            baseOffset[idx][0] = f[0] * offsetScale;
            baseOffset[idx][1] = -f[1] * offsetScale;
        }
    }

    std::array<std::vector<float>, 8> predicted{};
    std::array<cv::Mat, 8> predictedImgs;
    for (int h = 1; h <= 8; ++h) {
        std::vector<cv::Vec2f> offset(pixelCount, cv::Vec2f(0.0f, 0.0f));
        for (size_t i = 0; i < pixelCount; ++i) {
            offset[i][0] = std::round(baseOffset[i][0] * static_cast<float>(h));
            offset[i][1] = std::round(baseOffset[i][1] * static_cast<float>(h));
        }

        predicted[static_cast<size_t>(h - 1)] = moveBloomPixels(bloomTime1Binary, width, height, offset);
        predictedImgs[static_cast<size_t>(h - 1)] = buildBloomMapFromBinary(predicted[static_cast<size_t>(h - 1)], width, height);
    }

    std::vector<cv::Mat> forecastFrames;
    forecastFrames.reserve(8);
    for (int h = 1; h <= 8; ++h) {
        const fs::path predPath = outputDir / ("bloom_" + std::to_string(h) + "h.png");
        cv::Mat labeled = predictedImgs[static_cast<size_t>(h - 1)].clone();
        addPanelTitle(labeled, std::to_string(h) + " hour forecast");
        if (!cv::imwrite(predPath.string(), labeled)) return 1;
        forecastFrames.push_back(labeled);
    }

    const fs::path gridPath = outputDir / "bloom_forecast_grid.png";
    if (!cv::imwrite(gridPath.string(), buildForecastGrid(forecastFrames))) return 1;

    if (!EarlyWarning::generateEarlyWarningForecastImages(outputDir)) return 1;

    const fs::path gifPath = outputDir / "bloom_forecast.gif";
    const std::string gifCmd =
        "ffmpeg -y -framerate 1 -start_number 1 -i \"" + (outputDir / "bloom_%dh.png").string() +
        "\" -loop 0 \"" + gifPath.string() + "\" >/dev/null 2>&1";
    if (std::system(gifCmd.c_str()) != 0) return 1;

    if (!EarlyWarning::generateEarlyWarningGif(outputDir)) return 1;

    return 0;
}

#endif
