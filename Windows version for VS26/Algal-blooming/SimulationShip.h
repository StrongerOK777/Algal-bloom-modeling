#ifndef ALGAL_BLOOM_SIMULATION_SHIP_H
#define ALGAL_BLOOM_SIMULATION_SHIP_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "opencv2/imgcodecs.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/video/tracking.hpp"

namespace SimulationShip {

struct ShipConfig {
    cv::Point intake{758, 498};
    int primaryRadius = 10;
    int secondaryRadius = 20;
    int hpMax = 100;
    int pixelsPerShipPerStep = 30;
    int stepMinutes = 20;
    int totalHours = 4;
    int minShipsToTry = 1;
    int maxShipsToTry = 200;
};

struct ShipResult {
    bool success = false;
    int minShips = -1;
};

static bool isGreenBloom(const cv::Vec3b& px) {
    const int b = static_cast<int>(px[0]);
    const int g = static_cast<int>(px[1]);
    const int r = static_cast<int>(px[2]);
    return (g >= 100) && (g > b + 40) && (g > r + 40);
}

static bool isWhiteLand(const cv::Vec3b& px) {
    const int b = static_cast<int>(px[0]);
    const int g = static_cast<int>(px[1]);
    const int r = static_cast<int>(px[2]);
    return (b >= 240) && (g >= 240) && (r >= 240);
}

static cv::Mat bloomImageToLandMask(const cv::Mat& color) {
    cv::Mat landMask(color.rows, color.cols, CV_8UC1, cv::Scalar(0));
    for (int y = 0; y < color.rows; ++y) {
        for (int x = 0; x < color.cols; ++x) {
            if (isWhiteLand(color.at<cv::Vec3b>(y, x))) {
                landMask.at<unsigned char>(y, x) = 255;
            }
        }
    }
    return landMask;
}

static cv::Mat bloomImageToMask(const cv::Mat& color) {
    cv::Mat mask(color.rows, color.cols, CV_8UC1, cv::Scalar(0));
    for (int y = 0; y < color.rows; ++y) {
        for (int x = 0; x < color.cols; ++x) {
            if (isGreenBloom(color.at<cv::Vec3b>(y, x))) {
                mask.at<unsigned char>(y, x) = 255;
            }
        }
    }
    return mask;
}

static cv::Mat maskToBloomMap(const cv::Mat& mask, const cv::Mat& landMask) {
    const cv::Vec3b water(255, 179, 128);
    const cv::Vec3b bloom(0, 128, 0);
    const cv::Vec3b land(255, 255, 255);
    cv::Mat out(mask.rows, mask.cols, CV_8UC3, water);
    for (int y = 0; y < mask.rows; ++y) {
        for (int x = 0; x < mask.cols; ++x) {
            if (!landMask.empty() && landMask.at<unsigned char>(y, x) > 0) {
                out.at<cv::Vec3b>(y, x) = land;
                continue;
            }
            if (mask.at<unsigned char>(y, x) > 0) {
                out.at<cv::Vec3b>(y, x) = bloom;
            }
        }
    }
    return out;
}

static cv::Mat estimateFlowFromBloomPair(const cv::Mat& bloom1, const cv::Mat& bloom2) {
    cv::Mat prevGray;
    cv::Mat currGray;
    cv::cvtColor(bloom1, prevGray, cv::COLOR_BGR2GRAY);
    cv::cvtColor(bloom2, currGray, cv::COLOR_BGR2GRAY);

    cv::Mat flow;
    cv::calcOpticalFlowFarneback(
        prevGray,
        currGray,
        flow,
        0.5,
        5,
        80,
        10,
        7,
        1.5,
        cv::OPTFLOW_FARNEBACK_GAUSSIAN
    );
    return flow;
}

static void clearInsideSecondaryRing(cv::Mat& mask, const ShipConfig& cfg) {
    const int r2 = cfg.secondaryRadius;
    const int r2Sq = r2 * r2;
    for (int y = 0; y < mask.rows; ++y) {
        for (int x = 0; x < mask.cols; ++x) {
            const int dx = x - cfg.intake.x;
            const int dy = y - cfg.intake.y;
            if (dx * dx + dy * dy <= r2Sq) {
                mask.at<unsigned char>(y, x) = 0;
            }
        }
    }
}

static cv::Mat advectMaskByFlow20Min(const cv::Mat& currentMask, const cv::Mat& flow) {
    cv::Mat moved(currentMask.rows, currentMask.cols, CV_8UC1, cv::Scalar(0));
    const float scale = 1.0f / 3.0f;

    for (int y = 0; y < currentMask.rows; ++y) {
        for (int x = 0; x < currentMask.cols; ++x) {
            if (currentMask.at<unsigned char>(y, x) == 0) continue;

            const cv::Vec2f f = flow.at<cv::Vec2f>(y, x);
            const int xNew = std::clamp(
                static_cast<int>(std::lround(static_cast<float>(x) + f[0] * scale)),
                0,
                currentMask.cols - 1
            );
            const int yNew = std::clamp(
                static_cast<int>(std::lround(static_cast<float>(y) + f[1] * scale)),
                0,
                currentMask.rows - 1
            );

            cv::line(
                moved,
                cv::Point(x, y),
                cv::Point(xNew, yNew),
                cv::Scalar(255),
                1,
                cv::LINE_8
            );
        }
    }

    const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3));
    cv::morphologyEx(moved, moved, cv::MORPH_CLOSE, kernel, cv::Point(-1, -1), 1);
    return moved;
}

static int clearNearestBloomPixelsInSecondary(cv::Mat& mask, const ShipConfig& cfg, int ships) {
    const int capacity = ships * cfg.pixelsPerShipPerStep;
    if (capacity <= 0) return 0;

    std::vector<std::pair<int, cv::Point>> candidates;
    candidates.reserve(static_cast<size_t>(mask.rows) * static_cast<size_t>(mask.cols) / 8);

    const int r2Sq = cfg.secondaryRadius * cfg.secondaryRadius;
    for (int y = 0; y < mask.rows; ++y) {
        for (int x = 0; x < mask.cols; ++x) {
            if (mask.at<unsigned char>(y, x) == 0) continue;
            const int dx = x - cfg.intake.x;
            const int dy = y - cfg.intake.y;
            const int d2 = dx * dx + dy * dy;
            if (d2 <= r2Sq) {
                candidates.push_back({d2, cv::Point(x, y)});
            }
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    const int toClear = std::min(capacity, static_cast<int>(candidates.size()));
    for (int i = 0; i < toClear; ++i) {
        const cv::Point p = candidates[static_cast<size_t>(i)].second;
        mask.at<unsigned char>(p.y, p.x) = 0;
    }
    return toClear;
}

static void countDangerPixels(
    const cv::Mat& mask,
    const ShipConfig& cfg,
    int& innerCount,
    int& annulusCount
) {
    innerCount = 0;
    annulusCount = 0;

    const int r1Sq = cfg.primaryRadius * cfg.primaryRadius;
    const int r2Sq = cfg.secondaryRadius * cfg.secondaryRadius;

    for (int y = 0; y < mask.rows; ++y) {
        for (int x = 0; x < mask.cols; ++x) {
            if (mask.at<unsigned char>(y, x) == 0) continue;
            const int dx = x - cfg.intake.x;
            const int dy = y - cfg.intake.y;
            const int d2 = dx * dx + dy * dy;
            if (d2 <= r1Sq) {
                ++innerCount;
            } else if (d2 <= r2Sq) {
                ++annulusCount;
            }
        }
    }
}

static cv::Mat renderShipFrame(
    const cv::Mat& bloomMask,
    const cv::Mat& landMask,
    const ShipConfig& cfg,
    int step,
    int hp,
    int ships,
    int removedPixels,
    int innerCount,
    int annulusCount
) {
    cv::Mat frame = maskToBloomMap(bloomMask, landMask);

    cv::circle(frame, cfg.intake, cfg.secondaryRadius, cv::Scalar(0, 0, 255), 1, cv::LINE_AA);
    cv::circle(frame, cfg.intake, cfg.primaryRadius, cv::Scalar(0, 0, 255), 1, cv::LINE_AA);
    cv::drawMarker(frame, cfg.intake, cv::Scalar(0, 0, 255), cv::MARKER_STAR, 12, 1, cv::LINE_AA);

    const int elapsedMin = step * cfg.stepMinutes;
    const int baseHour = 11;
    const int baseMinute = 13;
    const int totalMinute = baseHour * 60 + baseMinute + elapsedMin;
    const int hh = (totalMinute / 60) % 24;
    const int mm = totalMinute % 60;
    const std::string timeText = (hh < 10 ? "0" : "") + std::to_string(hh) + ":" + (mm < 10 ? "0" : "") + std::to_string(mm);

    // Only display a 200x200 window centered at intake for output frames.
    const int windowSize = 200;
    const int half = windowSize / 2;
    const int srcX0 = std::max(0, cfg.intake.x - half);
    const int srcY0 = std::max(0, cfg.intake.y - half);
    const int srcX1 = std::min(frame.cols, cfg.intake.x + half);
    const int srcY1 = std::min(frame.rows, cfg.intake.y + half);

    cv::Mat cropped(windowSize, windowSize, frame.type(), cv::Scalar(255, 255, 255));
    const int copyW = std::max(0, srcX1 - srcX0);
    const int copyH = std::max(0, srcY1 - srcY0);
    const int dstX = std::max(0, half - (cfg.intake.x - srcX0));
    const int dstY = std::max(0, half - (cfg.intake.y - srcY0));

    if (copyW > 0 && copyH > 0 && dstX + copyW <= cropped.cols && dstY + copyH <= cropped.rows) {
        frame(cv::Rect(srcX0, srcY0, copyW, copyH)).copyTo(cropped(cv::Rect(dstX, dstY, copyW, copyH)));
    }

    // 放大并轻微锐化，提升裁切图可读性。
    cv::Mat enlarged;
    cv::resize(cropped, enlarged, cv::Size(), 2.0, 2.0, cv::INTER_CUBIC);
    cv::Mat sharp;
    const cv::Mat kernel = (cv::Mat_<float>(3, 3) <<
        0, -1,  0,
       -1,  5, -1,
        0, -1,  0);
    cv::filter2D(enlarged, sharp, -1, kernel);

    // 在最终输出图左上角添加状态信息。
    const int panelX = 10;
    const int panelY = 10;
    const int panelW = 320;
    const int panelH = 160;
    // cv::rectangle(sharp, cv::Rect(panelX, panelY, panelW, panelH), cv::Scalar(255, 255, 255), cv::FILLED);
    // cv::rectangle(sharp, cv::Rect(panelX, panelY, panelW, panelH), cv::Scalar(180, 180, 180), 1);

    cv::putText(sharp, "Ship Simulation", cv::Point(panelX + 10, panelY + 24), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 0, 0), 2, cv::LINE_AA);
    cv::putText(sharp, "Time: " + timeText, cv::Point(panelX + 10, panelY + 46), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0), 1, cv::LINE_AA);
    cv::putText(sharp, "Ships: " + std::to_string(ships), cv::Point(panelX + 10, panelY + 68), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0), 1, cv::LINE_AA);
    cv::putText(sharp, "HP: " + std::to_string(hp), cv::Point(panelX + 10, panelY + 90), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 255), 1, cv::LINE_AA);
    cv::putText(sharp, "Removed: " + std::to_string(removedPixels), cv::Point(panelX + 10, panelY + 112), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0), 1, cv::LINE_AA);
    cv::putText(sharp, "Inner: " + std::to_string(innerCount), cv::Point(panelX + 10, panelY + 134), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0), 1, cv::LINE_AA);
    //cv::putText(sharp, "Secondary: " + std::to_string(annulusCount), cv::Point(panelX + 150, panelY + 134), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0), 1, cv::LINE_AA);

    return sharp;
}

static bool runSingleShipCount(
    const cv::Mat& initialMask,
    const cv::Mat& landMask,
    const cv::Mat& flow,
    const ShipConfig& cfg,
    int ships,
    const std::filesystem::path& frameDir,
    bool saveFrames,
    bool& survived
) {
    cv::Mat mask = initialMask.clone();
    clearInsideSecondaryRing(mask, cfg);

    const int steps = (cfg.totalHours * 60) / cfg.stepMinutes;
    int hp = cfg.hpMax;
    survived = true;

    if (saveFrames) {
        std::filesystem::create_directories(frameDir);
        cv::Mat initFrame = renderShipFrame(mask, landMask, cfg, 0, hp, ships, 0, 0, 0);
        if (!cv::imwrite((frameDir / "ship_step_00.png").string(), initFrame)) {
            return false;
        }
    }

    for (int step = 1; step <= steps; ++step) {
        mask = advectMaskByFlow20Min(mask, flow);
        const int removed = clearNearestBloomPixelsInSecondary(mask, cfg, ships);

        int innerCount = 0;
        int annulusCount = 0;
        countDangerPixels(mask, cfg, innerCount, annulusCount);

        if (innerCount > 0) {
            hp = 0;
            survived = false;
        } else {
            hp = std::max(0, hp - annulusCount);
            if (hp <= 0) {
                survived = false;
            }
        }

        if (saveFrames) {
            const cv::Mat frame = renderShipFrame(mask, landMask, cfg, step, hp, ships, removed, innerCount, annulusCount);
            const std::string frameName =
                std::string("ship_step_") + (step < 10 ? "0" : "") + std::to_string(step) + ".png";
            if (!cv::imwrite((frameDir / frameName).string(), frame)) {
                return false;
            }
        }

        if (!survived) break;
    }

    return true;
}

static bool generateShipGif(const std::filesystem::path& frameDir, const std::filesystem::path& gifPath) {
    const std::string cmd =
        "ffmpeg -y -framerate 3 -start_number 0 -i \"" + (frameDir / "ship_step_%02d.png").string() +
        "\" -loop 0 \"" + gifPath.string() + "\"" +
#ifdef _WIN32
        " >NUL 2>&1";
#else
        " >/dev/null 2>&1";
#endif
    return std::system(cmd.c_str()) == 0;
}

static ShipResult runSimulation(
    const std::filesystem::path& outputDir = ".\\output",
    const ShipConfig& cfg = ShipConfig{}
) {
    ShipResult result;

    const cv::Mat bloom1 = cv::imread((outputDir / "bloom1.png").string(), cv::IMREAD_COLOR);
    const cv::Mat bloom2 = cv::imread((outputDir / "bloom2.png").string(), cv::IMREAD_COLOR);
    if (bloom1.empty() || bloom2.empty()) {
        return result;
    }

    const cv::Mat initialMask = bloomImageToMask(bloom1);
    const cv::Mat landMask = bloomImageToLandMask(bloom1);
    const cv::Mat flow = estimateFlowFromBloomPair(bloom1, bloom2);

    for (int ships = cfg.minShipsToTry; ships <= cfg.maxShipsToTry; ++ships) {
        bool survived = false;
        if (!runSingleShipCount(initialMask, landMask, flow, cfg, ships, outputDir / "ship_frames", false, survived)) {
            return result;
        }
        if (survived) {
            result.success = true;
            result.minShips = ships;
            break;
        }
    }
    if (!result.success) {
        return result;
    }

    bool survivedFinal = false;
    if (!runSingleShipCount(initialMask, landMask, flow, cfg, result.minShips, outputDir / "ship_frames", true, survivedFinal)) {
        result.success = false;
        result.minShips = -1;
        return result;
    }
    if (!survivedFinal) {
        result.success = false;
        result.minShips = -1;
        return result;
    }

    if (!generateShipGif(outputDir / "ship_frames", outputDir / "ship_simulation.gif")) {
        result.success = false;
        result.minShips = -1;
        return result;
    }

    cv::Mat summary(360, 900, CV_8UC3, cv::Scalar(240, 240, 240));
    cv::putText(summary, "Ship Simulation Result", cv::Point(30, 70), cv::FONT_HERSHEY_SIMPLEX, 1.4, cv::Scalar(20, 20, 20), 3, cv::LINE_AA);
    cv::putText(summary, "Time range: 11:13 - 15:13, step 20 min", cv::Point(30, 135), cv::FONT_HERSHEY_SIMPLEX, 0.9, cv::Scalar(30, 30, 30), 2, cv::LINE_AA);
    cv::putText(summary, "Minimum ships keeping HP > 0: " + std::to_string(result.minShips), cv::Point(30, 200), cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 0, 200), 2, cv::LINE_AA);
    cv::putText(summary, "Output GIF: ship_simulation.gif", cv::Point(30, 255), cv::FONT_HERSHEY_SIMPLEX, 0.9, cv::Scalar(30, 30, 30), 2, cv::LINE_AA);
    cv::imwrite((outputDir / "ship_simulation_summary.png").string(), summary);

    return result;
}

}  // namespace SimulationShip

#endif
