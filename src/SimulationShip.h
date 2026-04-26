#ifndef ALGAL_BLOOM_SIMULATION_SHIP_H
#define ALGAL_BLOOM_SIMULATION_SHIP_H

#include <filesystem>
#include "opencv2/core/types.hpp"

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

ShipResult runSimulation(
    const std::filesystem::path& outputDir = "./output",
    const ShipConfig& cfg = ShipConfig{}
);

}  // namespace SimulationShip

#endif
