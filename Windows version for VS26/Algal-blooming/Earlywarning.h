#ifndef EARLY_WARNING_H
#define EARLY_WARNING_H

#include <array>
#include <filesystem>
#include <queue>
#include <string>
#include <vector>

#include "opencv2/freetype.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/imgproc.hpp"

namespace EarlyWarning {

struct KeySite {
	std::string name;
	int col;
	int row;
	bool isWaterSource;
	int firstPollutedHour;
};

struct WarningEvent {
	std::string name;
	int hour;
};

static bool isBloomPixel(const cv::Mat& image, int row, int col) {
	if (image.empty()) return false;
	if (image.type() != CV_8UC3) return false;
	if (row < 0 || row >= image.rows || col < 0 || col >= image.cols) return false;

	const cv::Vec3b px = image.at<cv::Vec3b>(row, col);
	const int b = static_cast<int>(px[0]);
	const int g = static_cast<int>(px[1]);
	const int r = static_cast<int>(px[2]);
	return (g >= 100) && (g > b + 40) && (g > r + 40);
}

static bool hasBloomInRadius(
	const cv::Mat& image,
	int centerRow,
	int centerCol,
	int radius
) {
	if (image.empty()) return false;
	if (image.type() != CV_8UC3) return false;
	if (radius < 0) return false;

	const int r0 = std::max(0, centerRow - radius);
	const int r1 = std::min(image.rows - 1, centerRow + radius);
	const int c0 = std::max(0, centerCol - radius);
	const int c1 = std::min(image.cols - 1, centerCol + radius);
	const int radiusSq = radius * radius;

	for (int r = r0; r <= r1; ++r) {
		for (int c = c0; c <= c1; ++c) {
			const int dr = r - centerRow;
			const int dc = c - centerCol;
			if (dr * dr + dc * dc > radiusSq) continue;
			if (isBloomPixel(image, r, c)) return true;
		}
	}

	return false;
}

static cv::Ptr<cv::freetype::FreeType2> getChineseTextRenderer() {
	static cv::Ptr<cv::freetype::FreeType2> renderer;
	static bool initialized = false;
	if (initialized) return renderer;
	initialized = true;

	const std::array<std::string, 4> fontCandidates = {
		"C:\\Windows\\Fonts\\msyh.ttc",
		"C:\\Windows\\Fonts\\simhei.ttf",
		"C:\\Windows\\Fonts\\simsun.ttc",
		"C:\\Windows\\Fonts\\arialuni.ttf"
	};

	try {
		for (const std::string& path : fontCandidates) {
			if (!std::filesystem::exists(path)) continue;
			renderer = cv::freetype::createFreeType2();
			renderer->loadFontData(path, 0);
			return renderer;
		}
	} catch (const cv::Exception&) {
		renderer.release();
	}

	return renderer;
}

static cv::Size measureTextUtf8(
	const std::string& text,
	int fontHeight,
	int thickness,
	int* baseLine
) {
	const cv::Ptr<cv::freetype::FreeType2> renderer = getChineseTextRenderer();
	if (renderer) {
		return renderer->getTextSize(text, fontHeight, thickness, baseLine);
	}

	const double scale = static_cast<double>(fontHeight) / 30.0;
	return cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, scale, thickness, baseLine);
}

static void drawTextUtf8(
	cv::Mat& image,
	const std::string& text,
	const cv::Point& origin,
	int fontHeight,
	const cv::Scalar& color,
	int thickness
) {
	const cv::Ptr<cv::freetype::FreeType2> renderer = getChineseTextRenderer();
	if (renderer) {
		renderer->putText(image, text, origin, fontHeight, color, thickness, cv::LINE_AA, true);
		return;
	}

	const double scale = static_cast<double>(fontHeight) / 30.0;
	cv::putText(
		image,
		text,
		origin,
		cv::FONT_HERSHEY_SIMPLEX,
		scale,
		color,
		thickness,
		cv::LINE_AA
	);
}

static void drawWarningLinesBottomRight(cv::Mat& image, const std::vector<std::string>& lines) {
	if (image.empty() || lines.empty()) return;

	const int fontHeight = 24;
	const int thickness = 1;
	const int lineGap = 8;
	const int margin = 20;

	int baseLine = 0;
	int maxHeight = 0;
	for (const std::string& line : lines) {
		const cv::Size sz = measureTextUtf8(line, fontHeight, thickness, &baseLine);
		maxHeight = std::max(maxHeight, sz.height);
	}

	const int startY = std::max(
		0,
		image.rows - margin - static_cast<int>(lines.size()) * (maxHeight + lineGap) + maxHeight
	);

	for (size_t i = 0; i < lines.size(); ++i) {
		const cv::Size sz = measureTextUtf8(lines[i], fontHeight, thickness, &baseLine);
		const int x = std::max(0, image.cols - margin - sz.width - 12);
		const int y = startY + static_cast<int>(i) * (maxHeight + lineGap);
		drawTextUtf8(image, lines[i], cv::Point(x, y), fontHeight, cv::Scalar(0, 0, 255), thickness);
	}
}

static bool generateEarlyWarningForecastImages(const std::filesystem::path& outputDir) {
	std::vector<KeySite> sites = {
		{"沙渚水源地", 655, 334, true, -1},
		{"太湖镇水源地", 758, 498, true, -1},
		{"渔洋山水源地", 875, 741, true, -1},
		{"七里观光堤", 361, 350, false, -1},
		{"静山夕阳观景处", 651, 919, false,-1},
		{"香山景区", 77, 878, false, -1},
		{"太湖旅游度假区", 390, 1290, false, -1}
	};

	std::array<cv::Mat, 8> forecastFrames;
	std::queue<WarningEvent> rollingWarnings;

	for (int h = 1; h <= 8; ++h) {
		const std::filesystem::path srcPath = outputDir / ("bloom_" + std::to_string(h) + "h.png");
		cv::Mat frame = cv::imread(srcPath.string(), cv::IMREAD_COLOR);
		if (frame.empty()) return false;
		forecastFrames[static_cast<size_t>(h - 1)] = frame;

		for (KeySite& site : sites) {
			if (site.firstPollutedHour > 0) continue;
			const bool pollutedNow = site.isWaterSource
				? hasBloomInRadius(frame, site.row, site.col, 3)
				: hasBloomInRadius(frame, site.row, site.col, 1);
			if (pollutedNow) {
				site.firstPollutedHour = h;
				rollingWarnings.push({site.name, h});
			}
		}

		while (!rollingWarnings.empty() && h - rollingWarnings.front().hour >= 5) {
			rollingWarnings.pop();
		}

		cv::Mat out = forecastFrames[static_cast<size_t>(h - 1)].clone();

		for (const KeySite& site : sites) {
			const bool polluted = (site.firstPollutedHour > 0) && (site.firstPollutedHour <= h);
			const cv::Scalar color = polluted ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 255, 255);
			const int drawCol = std::clamp(site.col, 0, out.cols - 1);
			const int drawRow = std::clamp(site.row, 0, out.rows - 1);
			cv::circle(out, cv::Point(drawCol, drawRow), 20, color, 2, cv::LINE_AA);
		}

		std::vector<std::string> warningLines;
		warningLines.push_back("滚动预警(近5小时):");
		std::queue<WarningEvent> temp = rollingWarnings;
		while (!temp.empty()) {
			const WarningEvent evt = temp.front();
			temp.pop();
			warningLines.push_back(evt.name + " 在" + std::to_string(evt.hour) + "小时受污染");
		}

		if (warningLines.size() > 1) {
			drawWarningLinesBottomRight(out, warningLines);
		}

		const std::filesystem::path dstPath = outputDir / ("bloom_" + std::to_string(h) + "h_warning.png");
		if (!cv::imwrite(dstPath.string(), out)) return false;
	}

	return true;
}

static bool generateEarlyWarningGif(const std::filesystem::path& outputDir) {
	const std::filesystem::path warningGifPath = outputDir / "bloom_forecast_warning.gif";
	const std::string warningGifCmd =
		"ffmpeg -y -framerate 1 -start_number 1 -i \"" + (outputDir / "bloom_%dh_warning.png").string() +
		"\" -loop 0 \"" + warningGifPath.string() + "\"" +
#ifdef _WIN32
		" >NUL 2>&1";
#else
		" >/dev/null 2>&1";
#endif
	return std::system(warningGifCmd.c_str()) == 0;
}

}  // namespace EarlyWarning

#endif /* EARLY_WARNING_H */
