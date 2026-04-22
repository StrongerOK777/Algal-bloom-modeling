#include "paintorigin.h"
#include "paintcompare.h"
#include "Earlywarning.h"
#include "SimulationShip.h"
#include <cstdio>

int main() {
    printf("这是基础分部分：\n");
    if (runPaintOrigin(
            "./output",
            "../Example/GeoTIFF/2021_05_30_10_38_06_GF1.tif",
            "../Example/GeoTIFF/2021_05_30_11_13_47_GF4.tif") != 0) {
        return 1;
    }

    if (runPaintCompare(
            "./output",
            "../Example/GeoTIFF_Landmasked/2021_05_30_10_38_06_GF1.tif",
            "../Example/GeoTIFF_Landmasked/2021_05_30_11_13_47_GF4.tif") != 0) {
        return 1;
    }

    if (!EarlyWarning::generateEarlyWarningForecastImages("./output")) {
        return 1;
    }
    if (!EarlyWarning::generateEarlyWarningGif("./output")) {
        return 1;
    } 
    for(int i=1;i<=60;i++)printf("-");
    printf("\n加分项部分：\n");
    for(int i=1;i<=60;i++)printf("-");printf("\n");

    const SimulationShip::ShipResult shipResult = SimulationShip::runSimulation("./output");
    if (!shipResult.success) {
        printf("船只模拟失败：请确认 output 目录已生成 bloom1.png 与 bloom2.png。\n");
        return 1;
    }
    printf("四小时(20min步长)下血条始终>0的最少打捞船数量：%d\n", shipResult.minShips);
    printf("已输出 ship_simulation.gif 与 ship_simulation_summary.png\n");

    return 0;
}