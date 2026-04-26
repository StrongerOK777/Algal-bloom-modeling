#include "paintorigin.h"

#include "test.h"

#include <cstdlib>

int runPaintOrigin(
	const std::filesystem::path& outputDir,
	const std::string& gf1Path,
	const std::string& gf4Path
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
