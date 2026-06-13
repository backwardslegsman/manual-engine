#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace Assets::Assimp {
    struct ImportedVertex {
        glm::vec3 position{};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        glm::vec3 tangent{1.0f, 0.0f, 0.0f};
        glm::vec2 uv{};
    };

    struct ImportedMaterial {
        std::string name;
        glm::vec4 baseColorFactor{1.0f};
        float metallicFactor = 0.0f;
        float roughnessFactor = 1.0f;
        std::filesystem::path baseColorTexture;
        std::filesystem::path normalTexture;
        std::filesystem::path metallicTexture;
        std::filesystem::path roughnessTexture;
    };

    struct ImportedSubmesh {
        std::vector<ImportedVertex> vertices;
        std::vector<uint32_t> indices;
        uint32_t materialIndex = 0;
    };

    struct ImportedStaticMesh {
        std::vector<ImportedSubmesh> submeshes;
        std::vector<ImportedMaterial> materials;
    };

    struct ImportResult {
        bool success = false;
        std::string error;
        ImportedStaticMesh mesh;
    };

    ImportResult importStaticMesh(const std::filesystem::path& path);
}
