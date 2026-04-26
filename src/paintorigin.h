#ifndef ALGAL_BLOOM_PAINTORIGIN_H
#define ALGAL_BLOOM_PAINTORIGIN_H

#include <filesystem>
#include <string>

int runPaintOrigin(
    const std::filesystem::path& outputDir = "./output",
    const std::string& gf1Path = "../Example/GeoTIFF/2021_05_30_10_38_06_GF1.tif",
    const std::string& gf4Path = "../Example/GeoTIFF/2021_05_30_11_13_47_GF4.tif"
);

#endif
