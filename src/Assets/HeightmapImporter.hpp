#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Assets {
    enum class HeightmapSampleFormat {
        Unknown,
        UInt8,
        UInt16,
    };

    struct HeightmapImage {
        std::filesystem::path sourcePath;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t channels = 0;
        uint32_t bitDepth = 0;
        HeightmapSampleFormat sampleFormat = HeightmapSampleFormat::Unknown;
        std::vector<float> samples;
    };

    struct HeightmapImportResult {
        bool success = false;
        std::string message;
        HeightmapImage image;
        std::vector<std::string> warnings;
    };

    [[nodiscard]] HeightmapImportResult loadHeightmapImage(const std::filesystem::path& path);
}
