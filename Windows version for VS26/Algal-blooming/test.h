#ifndef ALGAL_BLOOM_TEST_H
#define ALGAL_BLOOM_TEST_H

#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include "gdal_priv.h"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/imgproc.hpp"

static std::pair<std::string, std::string> buildLabelText(const std::string& srcPath) {
    if (srcPath.find("GF4") != std::string::npos) {
        return {"2021-05-31T11:13", "Gaofen-4"};
    }
    if (srcPath.find("GF1") != std::string::npos) {
        return {"2021-05-31T10:38", "Gaofen-1"};
    }
    return {"", "Gaofen"};
}

static bool drawOverlayLabel(const char* outputPath, const std::string& srcPath) {
    cv::Mat img = cv::imread(outputPath, cv::IMREAD_COLOR);
    if (img.empty()) return false;

    const auto labels = buildLabelText(srcPath);
    const int x = 12;
    const int y0 = 42;
    const int y1 = 88;
    const int font = cv::FONT_HERSHEY_SIMPLEX;
    const double scale = 1.0;
    const int thickness = 2;
    const cv::Scalar white(255, 255, 255);

    if (!labels.first.empty()) {
        cv::putText(img, labels.first, cv::Point(x, y0), font, scale, white, thickness, cv::LINE_AA);
    }
    cv::putText(img, labels.second, cv::Point(x, y1), font, scale, white, thickness, cv::LINE_AA);

    return cv::imwrite(outputPath, img);
}

struct TifCube {
    int bands = 0;
    int width = 0;
    int height = 0;
    double geoTransform[6] = {0, 0, 0, 0, 0, 0};
    std::string projection;
    std::vector<float> data; // [band, y, x]
};

static double percentile(std::vector<float> values, double p) {
    if (values.empty()) return 0.0;
    p = std::clamp(p, 0.0, 100.0);
    const size_t idx = static_cast<size_t>(std::floor((p / 100.0) * static_cast<double>(values.size() - 1)));
    std::nth_element(values.begin(), values.begin() + idx, values.end());
    return static_cast<double>(values[idx]);
}

static bool tifReader(const std::string& path, TifCube& out) {
    GDALDataset* ds = static_cast<GDALDataset*>(GDALOpen(path.c_str(), GA_ReadOnly));
    if (!ds) return false;

    out.width = ds->GetRasterXSize();
    out.height = ds->GetRasterYSize();
    out.bands = ds->GetRasterCount();
    if (out.width <= 0 || out.height <= 0 || out.bands <= 0) {
        GDALClose(ds);
        return false;
    }

    ds->GetGeoTransform(out.geoTransform);
    const char* proj = ds->GetProjectionRef();
    out.projection = proj ? proj : "";

    out.data.assign(
        static_cast<size_t>(out.bands) * static_cast<size_t>(out.width) * static_cast<size_t>(out.height),
        0.0f
    );

    std::vector<int> bandMap(static_cast<size_t>(out.bands));
    for (int i = 0; i < out.bands; ++i) bandMap[static_cast<size_t>(i)] = i + 1;

    const CPLErr err = ds->RasterIO(
        GF_Read,
        0,
        0,
        out.width,
        out.height,
        out.data.data(),
        out.width,
        out.height,
        GDT_Float32,
        out.bands,
        bandMap.data(),
        sizeof(float),
        static_cast<GSpacing>(sizeof(float) * static_cast<size_t>(out.width)),
        static_cast<GSpacing>(sizeof(float) * static_cast<size_t>(out.width) * static_cast<size_t>(out.height))
    );

    GDALClose(ds);
    return err == CE_None;
}

static std::vector<float> truncatedLinearStretch(
    const float* gray,
    size_t pixelCount,
    double truncatedValue
) {
    std::vector<float> out(pixelCount, 0.0f);
    std::vector<float> valid;
    valid.reserve(pixelCount);

    for (size_t i = 0; i < pixelCount; ++i) {
        const float v = gray[i];
        if (std::isfinite(v)) valid.push_back(v);
    }

    if (valid.size() < 5) {
        for (size_t i = 0; i < pixelCount; ++i) {
            const float v = gray[i];
            out[i] = std::isfinite(v) ? v : 0.0f;
        }
        return out;
    }

    const double lo = percentile(valid, truncatedValue);
    const double hi = percentile(valid, 100.0 - truncatedValue);
    const double denom = (hi > lo) ? (hi - lo) : 1.0;

    for (size_t i = 0; i < pixelCount; ++i) {
        const float v = gray[i];
        if (!std::isfinite(v)) {
            out[i] = 0.0f;
            continue;
        }
        double x = (static_cast<double>(v) - lo) / denom;
        x = std::clamp(x, 0.0, 1.0);
        out[i] = static_cast<float>(x);
    }

    return out;
}

static int runTestFigure(const std::string& srcPath = "..\\Example\\GeoTIFF\\2021_05_30_10_38_06_GF1.tif"
                        ,const char* outputPath = ".\\output\\ThePic.png"
                        ,int stretchMode = -1) {
    GDALAllRegister();

    TifCube cube;
    if (!tifReader(srcPath, cube)) return 1;
    if (cube.bands < 4) return 1;

    const size_t pixelCount = static_cast<size_t>(cube.width) * static_cast<size_t>(cube.height);
    const size_t bandStride = pixelCount;
    
    int mode = stretchMode;
    if (mode < 0) {
        mode = (srcPath.find("GF4") != std::string::npos) ? 1 : 0;
    }
    const double per = (mode == 0) ? 2.0 : 4.0;

    const float* redBand = cube.data.data() + static_cast<size_t>(cube.bands - 2) * bandStride;
    const float* greenBand = cube.data.data() + static_cast<size_t>(cube.bands - 3) * bandStride;
    const float* blueBand = cube.data.data() + static_cast<size_t>(cube.bands - 4) * bandStride;

    std::vector<float> r = truncatedLinearStretch(redBand, pixelCount, per);
    std::vector<float> g = truncatedLinearStretch(greenBand, pixelCount, per);
    std::vector<float> b = truncatedLinearStretch(blueBand, pixelCount, per);

    const int maskRows = std::min(106, cube.height);
    const int maskCols = std::min(450, cube.width);
    for (int y = 0; y < maskRows; ++y) {
        for (int x = 0; x < maskCols; ++x) {
            const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(cube.width) + static_cast<size_t>(x);
            r[idx] = 0.0f;
            g[idx] = 0.0f;
            b[idx] = 0.0f;
        }
    }

    GDALDriver* memDrv = GetGDALDriverManager()->GetDriverByName("MEM");
    GDALDriver* pngDrv = GetGDALDriverManager()->GetDriverByName("PNG");
    if (!memDrv || !pngDrv) return 1;

    GDALDataset* mem = memDrv->Create("", cube.width, cube.height, 3, GDT_Byte, nullptr);
    if (!mem) return 1;

    std::vector<GByte> outBand(pixelCount, 0);
    const std::vector<float>* channels[3] = {&r, &g, &b};

    for (int c = 0; c < 3; ++c) {
        for (size_t i = 0; i < pixelCount; ++i) {
            double v = static_cast<double>((*channels[c])[i]);
            v = std::clamp(v, 0.0, 1.0);
            outBand[i] = static_cast<GByte>(std::lround(v * 255.0));
        }

        GDALRasterBand* db = mem->GetRasterBand(c + 1);
        if (!db || db->RasterIO(
                       GF_Write,
                       0,
                       0,
                       cube.width,
                       cube.height,
                       outBand.data(),
                       cube.width,
                       cube.height,
                       GDT_Byte,
                       0,
                       0,
                       nullptr
                   ) != CE_None) {
            GDALClose(mem);
            return 1;
        }
    }

    GDALDataset* out = pngDrv->CreateCopy(outputPath,mem, FALSE, nullptr, nullptr, nullptr);
    if (out) GDALClose(out);
    GDALClose(mem);
    if (!out) return 1;

    // Overlay labels like the Python version.
    if (!drawOverlayLabel(outputPath, srcPath)) return 1;
    return 0;
}

#endif
