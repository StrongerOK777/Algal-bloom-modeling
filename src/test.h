#ifndef ALGAL_BLOOM_TEST_H
#define ALGAL_BLOOM_TEST_H

#include <string>
#include <vector>

struct TifCube {
    int bands = 0;
    int width = 0;
    int height = 0;
    double geoTransform[6] = {0, 0, 0, 0, 0, 0};
    std::string projection;
    std::vector<float> data;
};

bool tifReader(const std::string& path, TifCube& out);

int runTestFigure(const std::string& srcPath = "../Example/GeoTIFF/2021_05_30_10_38_06_GF1.tif",
                  const char* outputPath = "./output/ThePic.png",
                  int stretchMode = -1);

#endif
