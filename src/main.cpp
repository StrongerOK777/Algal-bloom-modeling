#include "paintorigin.h"
#include "paintcompare.h"
int main() {
    runPaintOrigin("./output",
     "../Example/GeoTIFF/2021_05_30_10_38_06_GF1.tif",
     "../Example/GeoTIFF/2021_05_30_11_13_47_GF4.tif");

   runPaintCompare("./output",
     "../Example/GeoTIFF_Landmasked/2021_05_30_10_38_06_GF1.tif",
     "../Example/GeoTIFF_Landmasked/2021_05_30_11_13_47_GF4.tif");

    
    return 0;
}