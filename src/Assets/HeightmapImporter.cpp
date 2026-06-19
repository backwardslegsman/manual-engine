#include "Assets/HeightmapImporter.hpp"

#include <algorithm>
#include <limits>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace Assets {
    namespace {
        constexpr int MaxReasonableDimension = 65535;

        HeightmapImportResult fail(std::string message)
        {
            HeightmapImportResult result;
            result.message = std::move(message);
            return result;
        }
    }

    HeightmapImportResult loadHeightmapImage(const std::filesystem::path& path)
    {
        if (path.empty()) {
            return fail("Heightmap path is empty.");
        }

        int width = 0;
        int height = 0;
        int channels = 0;
        HeightmapImportResult result;
        result.image.sourcePath = path;

        const bool is16Bit = stbi_is_16_bit(path.string().c_str()) != 0;
        if (is16Bit) {
            stbi_us* pixels = stbi_load_16(path.string().c_str(), &width, &height, &channels, 0);
            if (!pixels) {
                return fail(stbi_failure_reason() ? stbi_failure_reason() : "Failed to decode 16-bit heightmap.");
            }
            if (width <= 0 || height <= 0 || channels <= 0 ||
                width > MaxReasonableDimension || height > MaxReasonableDimension) {
                stbi_image_free(pixels);
                return fail("Heightmap dimensions or channel count are unsupported.");
            }

            result.image.width = static_cast<uint32_t>(width);
            result.image.height = static_cast<uint32_t>(height);
            result.image.channels = static_cast<uint32_t>(channels);
            result.image.bitDepth = 16;
            result.image.sampleFormat = HeightmapSampleFormat::UInt16;
            const size_t sampleCount = static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(channels);
            result.image.samples.resize(sampleCount);
            constexpr float invMax = 1.0f / 65535.0f;
            for (size_t index = 0; index < sampleCount; ++index) {
                result.image.samples[index] = static_cast<float>(pixels[index]) * invMax;
            }
            stbi_image_free(pixels);
        } else {
            stbi_uc* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, 0);
            if (!pixels) {
                return fail(stbi_failure_reason() ? stbi_failure_reason() : "Failed to decode heightmap.");
            }
            if (width <= 0 || height <= 0 || channels <= 0 ||
                width > MaxReasonableDimension || height > MaxReasonableDimension) {
                stbi_image_free(pixels);
                return fail("Heightmap dimensions or channel count are unsupported.");
            }

            result.image.width = static_cast<uint32_t>(width);
            result.image.height = static_cast<uint32_t>(height);
            result.image.channels = static_cast<uint32_t>(channels);
            result.image.bitDepth = 8;
            result.image.sampleFormat = HeightmapSampleFormat::UInt8;
            const size_t sampleCount = static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(channels);
            result.image.samples.resize(sampleCount);
            constexpr float invMax = 1.0f / 255.0f;
            for (size_t index = 0; index < sampleCount; ++index) {
                result.image.samples[index] = static_cast<float>(pixels[index]) * invMax;
            }
            stbi_image_free(pixels);
        }

        result.success = true;
        result.message = "Heightmap decoded.";
        return result;
    }
}
