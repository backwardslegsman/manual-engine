#include "Engine/DebugVisualization.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Engine {
    namespace {
        [[nodiscard]] uint32_t categoryIndex(DebugVisualizationCategory category)
        {
            return static_cast<uint32_t>(category);
        }

        [[nodiscard]] uint32_t primitiveIndex(DebugVisualizationPrimitiveType type)
        {
            return static_cast<uint32_t>(type);
        }

        [[nodiscard]] bool finite(const glm::vec3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        [[nodiscard]] bool finite(const DebugVisualizationAabb& bounds)
        {
            return finite(bounds.min) && finite(bounds.max) &&
                bounds.min.x <= bounds.max.x &&
                bounds.min.y <= bounds.max.y &&
                bounds.min.z <= bounds.max.z;
        }

        [[nodiscard]] glm::vec3 centerOf(const DebugVisualizationAabb& bounds)
        {
            return (bounds.min + bounds.max) * 0.5f;
        }

        [[nodiscard]] uint32_t axisColor(int axis)
        {
            switch (axis) {
                case 0:
                    return 0xff3333ff;
                case 1:
                    return 0xff33ff33;
                case 2:
                    return 0xffff3333;
                default:
                    return 0xffffffff;
            }
        }
    }

    DebugVisualizationSettings defaultDebugVisualizationSettings()
    {
        DebugVisualizationSettings settings;
        for (DebugVisualizationCategorySettings& category : settings.categories) {
            category.enabled = true;
            category.maxPrimitives = 1024;
        }
        settings.categories[categoryIndex(DebugVisualizationCategory::Navigation)].maxPrimitives = 4096;
        settings.categories[categoryIndex(DebugVisualizationCategory::Terrain)].maxPrimitives = 4096;
        settings.categories[categoryIndex(DebugVisualizationCategory::Physics)].maxPrimitives = 2048;
        settings.categories[categoryIndex(DebugVisualizationCategory::Animation)].maxPrimitives = 2048;
        return settings;
    }

    const char* debugVisualizationCategoryName(DebugVisualizationCategory category)
    {
        switch (category) {
            case DebugVisualizationCategory::SceneTransforms:
                return "SceneTransforms";
            case DebugVisualizationCategory::SceneBounds:
                return "SceneBounds";
            case DebugVisualizationCategory::RenderBridge:
                return "RenderBridge";
            case DebugVisualizationCategory::Physics:
                return "Physics";
            case DebugVisualizationCategory::CharacterMovement:
                return "CharacterMovement";
            case DebugVisualizationCategory::Navigation:
                return "Navigation";
            case DebugVisualizationCategory::Terrain:
                return "Terrain";
            case DebugVisualizationCategory::Animation:
                return "Animation";
            case DebugVisualizationCategory::Assets:
                return "Assets";
            case DebugVisualizationCategory::BehaviorHooks:
                return "BehaviorHooks";
            case DebugVisualizationCategory::Lua:
                return "Lua";
            case DebugVisualizationCategory::Streaming:
                return "Streaming";
            case DebugVisualizationCategory::Serialization:
                return "Serialization";
            case DebugVisualizationCategory::Performance:
                return "Performance";
            case DebugVisualizationCategory::Count:
                break;
        }
        return "Unknown";
    }

    const char* debugVisualizationSeverityName(DebugVisualizationSeverity severity)
    {
        switch (severity) {
            case DebugVisualizationSeverity::Info:
                return "Info";
            case DebugVisualizationSeverity::Warning:
                return "Warning";
            case DebugVisualizationSeverity::Error:
                return "Error";
        }
        return "Unknown";
    }

    DebugVisualizationCollector::DebugVisualizationCollector(DebugVisualizationSettings settings)
        : settings_(std::move(settings))
    {
    }

    void DebugVisualizationCollector::setSettings(DebugVisualizationSettings settings)
    {
        settings_ = std::move(settings);
    }

    const DebugVisualizationSettings& DebugVisualizationCollector::settings() const
    {
        return settings_;
    }

    void DebugVisualizationCollector::clear()
    {
        diagnostics_ = {};
        lines_.clear();
        shapes_.clear();
        labels_.clear();
        reportRows_.clear();
    }

    DebugVisualizationDiagnostics DebugVisualizationCollector::diagnostics() const
    {
        return diagnostics_;
    }

    DebugVisualizationSnapshot DebugVisualizationCollector::snapshot() const
    {
        return {lines_, shapes_, labels_, reportRows_, diagnostics_};
    }

    bool DebugVisualizationCollector::addLine(
        DebugVisualizationCategory category,
        const glm::vec3& a,
        const glm::vec3& b,
        uint32_t color,
        DebugVisualizationSeverity severity,
        std::string source,
        std::string label)
    {
        if (!finite(a) || !finite(b)) {
            noteSkipped(category);
            return false;
        }
        const glm::vec3 anchor = (a + b) * 0.5f;
        if (!accepts(category, DebugVisualizationPrimitiveType::Line, anchor)) {
            return false;
        }
        noteSeverity(severity);
        lines_.push_back({category, severity, a, b, color, std::move(source), std::move(label)});
        return true;
    }

    bool DebugVisualizationCollector::addPolyline(
        DebugVisualizationCategory category,
        const std::vector<glm::vec3>& points,
        uint32_t color,
        DebugVisualizationSeverity severity,
        std::string source)
    {
        if (points.size() < 2) {
            noteSkipped(category);
            return false;
        }

        bool accepted = false;
        for (size_t index = 1; index < points.size(); ++index) {
            accepted = addLine(category, points[index - 1], points[index], color, severity, source) || accepted;
        }
        return accepted;
    }

    bool DebugVisualizationCollector::addAabb(
        DebugVisualizationCategory category,
        const DebugVisualizationAabb& bounds,
        uint32_t color,
        DebugVisualizationSeverity severity,
        std::string source,
        std::string label)
    {
        if (!finite(bounds)) {
            noteSkipped(category);
            return false;
        }
        if (!accepts(category, DebugVisualizationPrimitiveType::Aabb, centerOf(bounds))) {
            return false;
        }
        noteSeverity(severity);
        DebugVisualizationShape shape;
        shape.type = DebugVisualizationPrimitiveType::Aabb;
        shape.category = category;
        shape.severity = severity;
        shape.bounds = bounds;
        shape.color = color;
        shape.source = std::move(source);
        shape.label = std::move(label);
        shapes_.push_back(std::move(shape));
        return true;
    }

    bool DebugVisualizationCollector::addSphere(
        DebugVisualizationCategory category,
        const glm::vec3& center,
        float radius,
        uint32_t color,
        DebugVisualizationSeverity severity,
        std::string source,
        std::string label)
    {
        if (!finite(center) || !std::isfinite(radius) || radius <= 0.0f) {
            noteSkipped(category);
            return false;
        }
        if (!accepts(category, DebugVisualizationPrimitiveType::Sphere, center)) {
            return false;
        }
        noteSeverity(severity);
        DebugVisualizationShape shape;
        shape.type = DebugVisualizationPrimitiveType::Sphere;
        shape.category = category;
        shape.severity = severity;
        shape.position = center;
        shape.radius = radius;
        shape.color = color;
        shape.source = std::move(source);
        shape.label = std::move(label);
        shapes_.push_back(std::move(shape));
        return true;
    }

    bool DebugVisualizationCollector::addCapsule(
        DebugVisualizationCategory category,
        const glm::vec3& a,
        const glm::vec3& b,
        float radius,
        uint32_t color,
        DebugVisualizationSeverity severity,
        std::string source,
        std::string label)
    {
        if (!finite(a) || !finite(b) || !std::isfinite(radius) || radius <= 0.0f) {
            noteSkipped(category);
            return false;
        }
        if (!accepts(category, DebugVisualizationPrimitiveType::Capsule, (a + b) * 0.5f)) {
            return false;
        }
        noteSeverity(severity);
        DebugVisualizationShape shape;
        shape.type = DebugVisualizationPrimitiveType::Capsule;
        shape.category = category;
        shape.severity = severity;
        shape.position = a;
        shape.end = b;
        shape.radius = radius;
        shape.color = color;
        shape.source = std::move(source);
        shape.label = std::move(label);
        shapes_.push_back(std::move(shape));
        return true;
    }

    bool DebugVisualizationCollector::addTransformAxes(
        DebugVisualizationCategory category,
        const glm::mat4& transform,
        float axisLength,
        std::string source,
        std::string label)
    {
        const glm::vec3 origin{transform[3]};
        if (!finite(origin) || !std::isfinite(axisLength) || axisLength <= 0.0f) {
            noteSkipped(category);
            return false;
        }

        bool accepted = false;
        for (int axis = 0; axis < 3; ++axis) {
            const glm::vec3 direction = glm::vec3{transform[axis]} * axisLength;
            accepted = addLine(
                category,
                origin,
                origin + direction,
                axisColor(axis),
                DebugVisualizationSeverity::Info,
                source,
                axis == 0 ? label : std::string{}) || accepted;
        }
        return accepted;
    }

    bool DebugVisualizationCollector::addLabel(
        DebugVisualizationCategory category,
        const glm::vec3& position,
        std::string text,
        uint32_t color,
        DebugVisualizationSeverity severity,
        std::string source)
    {
        if (!finite(position) || text.empty()) {
            noteSkipped(category);
            return false;
        }
        if (!accepts(category, DebugVisualizationPrimitiveType::Label, position, true)) {
            return false;
        }
        noteSeverity(severity);
        labels_.push_back({category, severity, position, std::move(text), color, std::move(source)});
        diagnostics_.labelCount = static_cast<uint32_t>(labels_.size());
        return true;
    }

    bool DebugVisualizationCollector::addReportRow(
        DebugVisualizationCategory category,
        std::string source,
        std::string name,
        std::string value,
        DebugVisualizationSeverity severity,
        std::string message)
    {
        if (!settings_.enabled || !settings_.categories[categoryIndex(category)].enabled) {
            noteSkipped(category);
            return false;
        }
        if (reportRows_.size() >= settings_.maxReportRows) {
            noteCapped(category);
            return false;
        }
        noteAccepted(category, DebugVisualizationPrimitiveType::Label);
        noteSeverity(severity);
        reportRows_.push_back({category, severity, std::move(source), std::move(name), std::move(value), std::move(message)});
        diagnostics_.reportRowCount = static_cast<uint32_t>(reportRows_.size());
        return true;
    }

    bool DebugVisualizationCollector::accepts(
        DebugVisualizationCategory category,
        DebugVisualizationPrimitiveType type,
        const glm::vec3& anchor,
        bool countsAsLabel)
    {
        const uint32_t cat = categoryIndex(category);
        if (cat >= DebugVisualizationCategoryCount) {
            return false;
        }
        if (!settings_.enabled || !settings_.categories[cat].enabled) {
            noteSkipped(category);
            return false;
        }
        if (settings_.distanceClippingEnabled &&
            std::isfinite(settings_.maxDistance) &&
            settings_.maxDistance > 0.0f &&
            glm::distance(anchor, settings_.clipCenter) > settings_.maxDistance) {
            noteClipped(category);
            return false;
        }
        if (diagnostics_.acceptedPrimitiveCount >= settings_.maxPrimitives ||
            diagnostics_.acceptedByCategory[cat] >= settings_.categories[cat].maxPrimitives ||
            (countsAsLabel && labels_.size() >= settings_.maxLabels)) {
            noteCapped(category);
            return false;
        }
        noteAccepted(category, type);
        return true;
    }

    void DebugVisualizationCollector::noteAccepted(
        DebugVisualizationCategory category,
        DebugVisualizationPrimitiveType type)
    {
        ++diagnostics_.acceptedPrimitiveCount;
        ++diagnostics_.acceptedByCategory[categoryIndex(category)];
        ++diagnostics_.acceptedByPrimitiveType[primitiveIndex(type)];
    }

    void DebugVisualizationCollector::noteSkipped(DebugVisualizationCategory category)
    {
        ++diagnostics_.skippedPrimitiveCount;
        ++diagnostics_.skippedByCategory[categoryIndex(category)];
    }

    void DebugVisualizationCollector::noteClipped(DebugVisualizationCategory category)
    {
        ++diagnostics_.clippedPrimitiveCount;
        ++diagnostics_.clippedByCategory[categoryIndex(category)];
    }

    void DebugVisualizationCollector::noteCapped(DebugVisualizationCategory category)
    {
        ++diagnostics_.cappedPrimitiveCount;
        ++diagnostics_.cappedByCategory[categoryIndex(category)];
        diagnostics_.lastCappedCategory = category;
    }

    void DebugVisualizationCollector::noteSeverity(DebugVisualizationSeverity severity)
    {
        if (severity == DebugVisualizationSeverity::Warning) {
            ++diagnostics_.warningCount;
        } else if (severity == DebugVisualizationSeverity::Error) {
            ++diagnostics_.errorCount;
        }
    }

    std::vector<DebugVisualizationExpandedLine> expandDebugVisualizationLines(
        const DebugVisualizationSnapshot& snapshot)
    {
        std::vector<DebugVisualizationExpandedLine> lines;
        lines.reserve(snapshot.lines.size() + snapshot.shapes.size() * 48);
        const auto pushLine = [&](const glm::vec3& a,
                                  const glm::vec3& b,
                                  uint32_t color,
                                  DebugVisualizationCategory category,
                                  DebugVisualizationSeverity severity,
                                  const std::string& source,
                                  const std::string& label) {
            lines.push_back({a, b, color, category, severity, source, label});
        };

        for (const DebugVisualizationLine& line : snapshot.lines) {
            pushLine(line.a, line.b, line.color, line.category, line.severity, line.source, line.label);
        }

        for (const DebugVisualizationShape& shape : snapshot.shapes) {
            if (shape.type == DebugVisualizationPrimitiveType::Aabb) {
                const glm::vec3& mn = shape.bounds.min;
                const glm::vec3& mx = shape.bounds.max;
                const glm::vec3 corners[8] = {
                    {mn.x, mn.y, mn.z},
                    {mx.x, mn.y, mn.z},
                    {mx.x, mn.y, mx.z},
                    {mn.x, mn.y, mx.z},
                    {mn.x, mx.y, mn.z},
                    {mx.x, mx.y, mn.z},
                    {mx.x, mx.y, mx.z},
                    {mn.x, mx.y, mx.z},
                };
                constexpr int edges[12][2] = {
                    {0, 1}, {1, 2}, {2, 3}, {3, 0},
                    {4, 5}, {5, 6}, {6, 7}, {7, 4},
                    {0, 4}, {1, 5}, {2, 6}, {3, 7},
                };
                for (const auto& edge : edges) {
                    pushLine(
                        corners[edge[0]],
                        corners[edge[1]],
                        shape.color,
                        shape.category,
                        shape.severity,
                        shape.source,
                        shape.label);
                }
            } else if (shape.type == DebugVisualizationPrimitiveType::Sphere) {
                constexpr int Segments = 16;
                constexpr float TwoPi = 6.28318530717958647692f;
                for (int axis = 0; axis < 3; ++axis) {
                    for (int segment = 0; segment < Segments; ++segment) {
                        const float a0 = TwoPi * static_cast<float>(segment) / static_cast<float>(Segments);
                        const float a1 = TwoPi * static_cast<float>(segment + 1) / static_cast<float>(Segments);
                        glm::vec3 p0 = shape.position;
                        glm::vec3 p1 = shape.position;
                        if (axis == 0) {
                            p0 += glm::vec3{0.0f, std::cos(a0), std::sin(a0)} * shape.radius;
                            p1 += glm::vec3{0.0f, std::cos(a1), std::sin(a1)} * shape.radius;
                        } else if (axis == 1) {
                            p0 += glm::vec3{std::cos(a0), 0.0f, std::sin(a0)} * shape.radius;
                            p1 += glm::vec3{std::cos(a1), 0.0f, std::sin(a1)} * shape.radius;
                        } else {
                            p0 += glm::vec3{std::cos(a0), std::sin(a0), 0.0f} * shape.radius;
                            p1 += glm::vec3{std::cos(a1), std::sin(a1), 0.0f} * shape.radius;
                        }
                        pushLine(p0, p1, shape.color, shape.category, shape.severity, shape.source, shape.label);
                    }
                }
            } else if (shape.type == DebugVisualizationPrimitiveType::Capsule) {
                constexpr int Segments = 16;
                constexpr float TwoPi = 6.28318530717958647692f;
                const glm::vec3 axis = glm::length(shape.end - shape.position) > 0.0001f
                    ? glm::normalize(shape.end - shape.position)
                    : glm::vec3{0.0f, 1.0f, 0.0f};
                const glm::vec3 helper = std::abs(axis.y) < 0.95f ? glm::vec3{0.0f, 1.0f, 0.0f} : glm::vec3{1.0f, 0.0f, 0.0f};
                const glm::vec3 right = glm::normalize(glm::cross(helper, axis));
                const glm::vec3 forward = glm::normalize(glm::cross(axis, right));
                for (int segment = 0; segment < Segments; ++segment) {
                    const float a0 = TwoPi * static_cast<float>(segment) / static_cast<float>(Segments);
                    const float a1 = TwoPi * static_cast<float>(segment + 1) / static_cast<float>(Segments);
                    const glm::vec3 r0 = (std::cos(a0) * right + std::sin(a0) * forward) * shape.radius;
                    const glm::vec3 r1 = (std::cos(a1) * right + std::sin(a1) * forward) * shape.radius;
                    pushLine(shape.position + r0, shape.position + r1, shape.color, shape.category, shape.severity, shape.source, shape.label);
                    pushLine(shape.end + r0, shape.end + r1, shape.color, shape.category, shape.severity, shape.source, shape.label);
                    if (segment % 4 == 0) {
                        pushLine(shape.position + r0, shape.end + r0, shape.color, shape.category, shape.severity, shape.source, shape.label);
                    }
                }
            }
        }
        return lines;
    }
}
