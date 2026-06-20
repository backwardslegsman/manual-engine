#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Renderer {
    struct DebugLinePrimitive;
}

namespace Engine {
    enum class DebugVisualizationCategory : uint32_t {
        SceneTransforms,
        SceneBounds,
        RenderBridge,
        Physics,
        CharacterMovement,
        Navigation,
        Terrain,
        Animation,
        Assets,
        BehaviorHooks,
        Lua,
        Streaming,
        Serialization,
        Performance,
        Count,
    };

    enum class DebugVisualizationSeverity : uint32_t {
        Info,
        Warning,
        Error,
    };

    enum class DebugVisualizationPrimitiveType : uint32_t {
        Line,
        Aabb,
        Sphere,
        Capsule,
        TransformAxes,
        Label,
        Path,
        MeshWireSummary,
        SectorBounds,
        Count,
    };

    inline constexpr uint32_t DebugVisualizationCategoryCount =
        static_cast<uint32_t>(DebugVisualizationCategory::Count);
    inline constexpr uint32_t DebugVisualizationPrimitiveTypeCount =
        static_cast<uint32_t>(DebugVisualizationPrimitiveType::Count);

    struct DebugVisualizationAabb {
        glm::vec3 min{0.0f};
        glm::vec3 max{0.0f};
    };

    struct DebugVisualizationCategorySettings {
        bool enabled = true;
        uint32_t maxPrimitives = 1024;
    };

    struct DebugVisualizationSettings {
        bool enabled = true;
        uint32_t maxPrimitives = 8192;
        uint32_t maxLabels = 512;
        uint32_t maxReportRows = 1024;
        bool distanceClippingEnabled = false;
        glm::vec3 clipCenter{0.0f};
        float maxDistance = 0.0f;
        std::array<DebugVisualizationCategorySettings, DebugVisualizationCategoryCount> categories{};
    };

    struct DebugVisualizationDiagnostics {
        uint32_t acceptedPrimitiveCount = 0;
        uint32_t skippedPrimitiveCount = 0;
        uint32_t clippedPrimitiveCount = 0;
        uint32_t cappedPrimitiveCount = 0;
        uint32_t labelCount = 0;
        uint32_t reportRowCount = 0;
        uint32_t warningCount = 0;
        uint32_t errorCount = 0;
        std::optional<DebugVisualizationCategory> lastCappedCategory;
        std::array<uint32_t, DebugVisualizationCategoryCount> acceptedByCategory{};
        std::array<uint32_t, DebugVisualizationCategoryCount> skippedByCategory{};
        std::array<uint32_t, DebugVisualizationCategoryCount> clippedByCategory{};
        std::array<uint32_t, DebugVisualizationCategoryCount> cappedByCategory{};
        std::array<uint32_t, DebugVisualizationPrimitiveTypeCount> acceptedByPrimitiveType{};
    };

    struct DebugVisualizationStyle {
        uint32_t color = 0xffffffff;
        DebugVisualizationSeverity severity = DebugVisualizationSeverity::Info;
        std::string source;
        std::string label;
    };

    struct DebugVisualizationLine {
        DebugVisualizationCategory category = DebugVisualizationCategory::SceneTransforms;
        DebugVisualizationSeverity severity = DebugVisualizationSeverity::Info;
        glm::vec3 a{0.0f};
        glm::vec3 b{0.0f};
        uint32_t color = 0xffffffff;
        std::string source;
        std::string label;
    };

    struct DebugVisualizationShape {
        DebugVisualizationPrimitiveType type = DebugVisualizationPrimitiveType::Aabb;
        DebugVisualizationCategory category = DebugVisualizationCategory::SceneBounds;
        DebugVisualizationSeverity severity = DebugVisualizationSeverity::Info;
        DebugVisualizationAabb bounds{};
        glm::vec3 position{0.0f};
        glm::vec3 end{0.0f};
        glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 halfExtents{0.0f};
        float radius = 0.0f;
        float height = 0.0f;
        uint32_t color = 0xffffffff;
        std::string source;
        std::string label;
    };

    struct DebugVisualizationLabel {
        DebugVisualizationCategory category = DebugVisualizationCategory::SceneTransforms;
        DebugVisualizationSeverity severity = DebugVisualizationSeverity::Info;
        glm::vec3 position{0.0f};
        std::string text;
        uint32_t color = 0xffffffff;
        std::string source;
    };

    struct DebugVisualizationReportRow {
        DebugVisualizationCategory category = DebugVisualizationCategory::Performance;
        DebugVisualizationSeverity severity = DebugVisualizationSeverity::Info;
        std::string source;
        std::string name;
        std::string value;
        std::string message;
    };

    struct DebugVisualizationSnapshot {
        std::vector<DebugVisualizationLine> lines;
        std::vector<DebugVisualizationShape> shapes;
        std::vector<DebugVisualizationLabel> labels;
        std::vector<DebugVisualizationReportRow> reportRows;
        DebugVisualizationDiagnostics diagnostics;
    };

    [[nodiscard]] DebugVisualizationSettings defaultDebugVisualizationSettings();
    [[nodiscard]] const char* debugVisualizationCategoryName(DebugVisualizationCategory category);
    [[nodiscard]] const char* debugVisualizationSeverityName(DebugVisualizationSeverity severity);

    class DebugVisualizationCollector {
    public:
        explicit DebugVisualizationCollector(DebugVisualizationSettings settings = defaultDebugVisualizationSettings());

        void setSettings(DebugVisualizationSettings settings);
        [[nodiscard]] const DebugVisualizationSettings& settings() const;

        void clear();
        [[nodiscard]] DebugVisualizationDiagnostics diagnostics() const;
        [[nodiscard]] DebugVisualizationSnapshot snapshot() const;

        bool addLine(
            DebugVisualizationCategory category,
            const glm::vec3& a,
            const glm::vec3& b,
            uint32_t color = 0xffffffff,
            DebugVisualizationSeverity severity = DebugVisualizationSeverity::Info,
            std::string source = {},
            std::string label = {});
        bool addPolyline(
            DebugVisualizationCategory category,
            const std::vector<glm::vec3>& points,
            uint32_t color = 0xffffffff,
            DebugVisualizationSeverity severity = DebugVisualizationSeverity::Info,
            std::string source = {});
        bool addAabb(
            DebugVisualizationCategory category,
            const DebugVisualizationAabb& bounds,
            uint32_t color = 0xffffffff,
            DebugVisualizationSeverity severity = DebugVisualizationSeverity::Info,
            std::string source = {},
            std::string label = {});
        bool addSphere(
            DebugVisualizationCategory category,
            const glm::vec3& center,
            float radius,
            uint32_t color = 0xffffffff,
            DebugVisualizationSeverity severity = DebugVisualizationSeverity::Info,
            std::string source = {},
            std::string label = {});
        bool addCapsule(
            DebugVisualizationCategory category,
            const glm::vec3& a,
            const glm::vec3& b,
            float radius,
            uint32_t color = 0xffffffff,
            DebugVisualizationSeverity severity = DebugVisualizationSeverity::Info,
            std::string source = {},
            std::string label = {});
        bool addTransformAxes(
            DebugVisualizationCategory category,
            const glm::mat4& transform,
            float axisLength = 1.0f,
            std::string source = {},
            std::string label = {});
        bool addLabel(
            DebugVisualizationCategory category,
            const glm::vec3& position,
            std::string text,
            uint32_t color = 0xffffffff,
            DebugVisualizationSeverity severity = DebugVisualizationSeverity::Info,
            std::string source = {});
        bool addReportRow(
            DebugVisualizationCategory category,
            std::string source,
            std::string name,
            std::string value,
            DebugVisualizationSeverity severity = DebugVisualizationSeverity::Info,
            std::string message = {});

    private:
        [[nodiscard]] bool accepts(
            DebugVisualizationCategory category,
            DebugVisualizationPrimitiveType type,
            const glm::vec3& anchor,
            bool countsAsLabel = false);
        void noteAccepted(DebugVisualizationCategory category, DebugVisualizationPrimitiveType type);
        void noteSkipped(DebugVisualizationCategory category);
        void noteClipped(DebugVisualizationCategory category);
        void noteCapped(DebugVisualizationCategory category);
        void noteSeverity(DebugVisualizationSeverity severity);

        DebugVisualizationSettings settings_;
        DebugVisualizationDiagnostics diagnostics_;
        std::vector<DebugVisualizationLine> lines_;
        std::vector<DebugVisualizationShape> shapes_;
        std::vector<DebugVisualizationLabel> labels_;
        std::vector<DebugVisualizationReportRow> reportRows_;
    };

    [[nodiscard]] std::vector<Renderer::DebugLinePrimitive> toRendererDebugLines(
        const DebugVisualizationSnapshot& snapshot);
}
