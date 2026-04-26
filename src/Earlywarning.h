#ifndef EARLY_WARNING_H
#define EARLY_WARNING_H

#include <filesystem>

namespace EarlyWarning {
bool generateEarlyWarningForecastImages(const std::filesystem::path& outputDir);
bool generateEarlyWarningGif(const std::filesystem::path& outputDir);

}  // namespace EarlyWarning

#endif /* EARLY_WARNING_H */
