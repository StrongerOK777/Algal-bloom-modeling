#ifndef ALGAL_BLOOM_PAINTORIGIN_H
#define ALGAL_BLOOM_PAINTORIGIN_H

#include "test.h"

#include <filesystem>
#include <string>
#include <cstdlib>

static int runPaintOrigin(
    const std::filesystem::path& outputDir = "./output",
    const std::string& gf1Path = "../Example/GeoTIFF/2021_05_30_10_38_06_GF1.tif",
    const std::string& gf4Path = "../Example/GeoTIFF/2021_05_30_11_13_47_GF4.tif"
) {
    namespace fs = std::filesystem;

    fs::create_directories(outputDir);

    const fs::path pic1 = outputDir / "Pic1.png";
    const fs::path pic2 = outputDir / "Pic2.png";
    const fs::path gifPath = outputDir / "satellite.gif";

    if (runTestFigure(gf1Path, pic1.string().c_str()) != 0) {
        return 1;
    }
    if (runTestFigure(gf4Path, pic2.string().c_str()) != 0) {
        return 1;
    }

    const std::string cmd =
        "ffmpeg -y -framerate 1.2 -start_number 1 -i \"" + (outputDir / "Pic%d.png").string() +
        "\" -loop 0 \"" + gifPath.string() + "\"";

    const int code = std::system(cmd.c_str());
    return (code == 0) ? 0 : 1;
}

#endif
