#include "App/EditorUi.hpp"
#include "App/EditorToolScripting.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string_view>

#ifndef MANUAL_ENGINE_ENABLE_DEBUG_TOOLS
#define MANUAL_ENGINE_ENABLE_DEBUG_TOOLS 1
#endif

#if MANUAL_ENGINE_ENABLE_DEBUG_TOOLS
#include <imgui.h>
#endif

namespace ManualEngine::App {
    namespace {
        [[nodiscard]] std::string toLower(std::string_view value)
        {
            std::string out;
            out.reserve(value.size());
            for (char c : value) {
                out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            return out;
        }

        [[nodiscard]] bool containsInsensitive(std::string_view haystack, std::string_view needle)
        {
            if (needle.empty()) {
                return true;
            }
            return toLower(haystack).find(toLower(needle)) != std::string::npos;
        }

        [[nodiscard]] bool valuesEqual(const Engine::ReflectedValue& lhs, const Engine::ReflectedValue& rhs)
        {
            return lhs == rhs;
        }

        [[nodiscard]] std::string reflectionStatusName(Engine::ReflectionStatus status)
        {
            switch (status) {
                case Engine::ReflectionStatus::Success: return "Success";
                case Engine::ReflectionStatus::NotFound: return "NotFound";
                case Engine::ReflectionStatus::DuplicateObject: return "DuplicateObject";
                case Engine::ReflectionStatus::DuplicateProperty: return "DuplicateProperty";
                case Engine::ReflectionStatus::InvalidHandle: return "InvalidHandle";
                case Engine::ReflectionStatus::WrongHandleKind: return "WrongHandleKind";
                case Engine::ReflectionStatus::WrongOwner: return "WrongOwner";
                case Engine::ReflectionStatus::UnknownProperty: return "UnknownProperty";
                case Engine::ReflectionStatus::TypeMismatch: return "TypeMismatch";
                case Engine::ReflectionStatus::ReadOnly: return "ReadOnly";
                case Engine::ReflectionStatus::ValidationFailed: return "ValidationFailed";
                case Engine::ReflectionStatus::Unsupported: return "Unsupported";
                case Engine::ReflectionStatus::SubsystemRejected: return "SubsystemRejected";
            }
            return "Unknown";
        }

        [[nodiscard]] std::string valueText(const Engine::ReflectedValue& value)
        {
            std::ostringstream stream;
            if (const auto* v = std::get_if<bool>(&value)) {
                return *v ? "true" : "false";
            }
            if (const auto* v = std::get_if<int64_t>(&value)) {
                stream << *v;
                return stream.str();
            }
            if (const auto* v = std::get_if<uint64_t>(&value)) {
                stream << *v;
                return stream.str();
            }
            if (const auto* v = std::get_if<float>(&value)) {
                stream << *v;
                return stream.str();
            }
            if (const auto* v = std::get_if<std::string>(&value)) {
                return *v;
            }
            if (const auto* v = std::get_if<glm::vec2>(&value)) {
                stream << v->x << ", " << v->y;
                return stream.str();
            }
            if (const auto* v = std::get_if<glm::vec3>(&value)) {
                stream << v->x << ", " << v->y << ", " << v->z;
                return stream.str();
            }
            return "<unsupported>";
        }

        [[nodiscard]] bool supportedType(Engine::ReflectedValueType type)
        {
            switch (type) {
                case Engine::ReflectedValueType::Bool:
                case Engine::ReflectedValueType::Int64:
                case Engine::ReflectedValueType::UInt64:
                case Engine::ReflectedValueType::Float:
                case Engine::ReflectedValueType::String:
                case Engine::ReflectedValueType::Vec2:
                case Engine::ReflectedValueType::Vec3:
                    return true;
                default:
                    return false;
            }
        }

        [[nodiscard]] std::string rowSearchText(const EditorUiPropertyRow& row)
        {
            return row.objectDisplayName + " " + row.category + " " + row.displayName + " " + row.propertyName;
        }

        void advanceRebuildBaseline(EditorUiState& state)
        {
            state.baselineSettings.streaming = state.settings.streaming;
        }

        void advanceLightweightBaseline(EditorUiState& state)
        {
            state.baselineSettings.renderer = state.settings.renderer;
            state.baselineSettings.debugDraw = state.settings.debugDraw;
            state.baselineSettings.camera = state.settings.camera;
        }

        void appendValidationDiagnostics(
            EditorRebuildCoordinatorState& coordinator,
            const EditorProjectSettingsValidationResult& validation)
        {
            coordinator.diagnostics.clear();
            for (const std::string& error : validation.errors) {
                coordinator.diagnostics.push_back("Error: " + error);
            }
            for (const std::string& warning : validation.warnings) {
                coordinator.diagnostics.push_back("Warning: " + warning);
            }
            if (coordinator.diagnostics.empty()) {
                coordinator.diagnostics.push_back("Current editor profile is valid.");
            }
        }

        void emitCommandEvent(
            EditorUiState& state,
            EditorLiveApplyHost* host,
            EditorToolEventType type,
            bool success,
            std::string message,
            EditorRebuildCommand command)
        {
            EditorToolEvent event;
            event.type = type;
            event.success = success;
            event.message = std::move(message);
            event.command = command;
            emitEditorToolEvent(state, host, event);
        }

#if MANUAL_ENGINE_ENABLE_DEBUG_TOOLS
        [[nodiscard]] std::string widgetId(const EditorUiPropertyRow& row)
        {
            return row.displayName + "##" + std::to_string(static_cast<uint32_t>(row.target.object)) + "_" +
                std::to_string(row.target.index) + "_" + std::to_string(static_cast<uint32_t>(row.property));
        }

        template <typename T>
        [[nodiscard]] bool scalarBounds(
            const Engine::ReflectedPropertyDescriptor& descriptor,
            T& minValue,
            T& maxValue)
        {
            const T* min = descriptor.minimum ? std::get_if<T>(&*descriptor.minimum) : nullptr;
            const T* max = descriptor.maximum ? std::get_if<T>(&*descriptor.maximum) : nullptr;
            if (!min || !max) {
                return false;
            }
            minValue = *min;
            maxValue = *max;
            return true;
        }

        bool drawValueWidget(
            EditorUiState& state,
            const EditorUiPropertyRow& row,
            const Engine::ReflectedPropertyDescriptor& descriptor)
        {
            if (row.readOnly || !row.supported) {
                ImGui::BeginDisabled();
                ImGui::TextUnformatted(valueText(row.currentValue).c_str());
                ImGui::EndDisabled();
                return false;
            }

            const std::string id = widgetId(row);
            switch (row.type) {
                case Engine::ReflectedValueType::Bool: {
                    bool value = std::get<bool>(row.currentValue);
                    if (ImGui::Checkbox(id.c_str(), &value)) {
                        (void)setEditorUiValue(state, row.target, row.property, value);
                        return true;
                    }
                    return false;
                }
                case Engine::ReflectedValueType::Float: {
                    float value = std::get<float>(row.currentValue);
                    float minValue = 0.0f;
                    float maxValue = 0.0f;
                    bool changed = false;
                    if (scalarBounds(descriptor, minValue, maxValue)) {
                        changed = ImGui::SliderFloat(id.c_str(), &value, minValue, maxValue);
                    } else {
                        changed = ImGui::InputFloat(id.c_str(), &value);
                    }
                    if (changed) {
                        (void)setEditorUiValue(state, row.target, row.property, value);
                        return true;
                    }
                    return false;
                }
                case Engine::ReflectedValueType::Int64: {
                    int64_t value = std::get<int64_t>(row.currentValue);
                    int64_t minValue = 0;
                    int64_t maxValue = 0;
                    bool changed = false;
                    if (scalarBounds(descriptor, minValue, maxValue)) {
                        changed = ImGui::SliderScalar(id.c_str(), ImGuiDataType_S64, &value, &minValue, &maxValue);
                    } else {
                        changed = ImGui::InputScalar(id.c_str(), ImGuiDataType_S64, &value);
                    }
                    if (changed) {
                        (void)setEditorUiValue(state, row.target, row.property, value);
                        return true;
                    }
                    return false;
                }
                case Engine::ReflectedValueType::UInt64: {
                    uint64_t value = std::get<uint64_t>(row.currentValue);
                    uint64_t minValue = 0;
                    uint64_t maxValue = 0;
                    bool changed = false;
                    if (scalarBounds(descriptor, minValue, maxValue)) {
                        changed = ImGui::SliderScalar(id.c_str(), ImGuiDataType_U64, &value, &minValue, &maxValue);
                    } else {
                        changed = ImGui::InputScalar(id.c_str(), ImGuiDataType_U64, &value);
                    }
                    if (changed) {
                        (void)setEditorUiValue(state, row.target, row.property, value);
                        return true;
                    }
                    return false;
                }
                case Engine::ReflectedValueType::String: {
                    const std::string current = std::get<std::string>(row.currentValue);
                    if (!row.enumLabels.empty()) {
                        int selected = 0;
                        for (int index = 0; index < static_cast<int>(row.enumLabels.size()); ++index) {
                            if (row.enumLabels[static_cast<size_t>(index)] == current) {
                                selected = index;
                                break;
                            }
                        }
                        if (ImGui::BeginCombo(id.c_str(), current.c_str())) {
                            for (int index = 0; index < static_cast<int>(row.enumLabels.size()); ++index) {
                                const bool isSelected = selected == index;
                                if (ImGui::Selectable(row.enumLabels[static_cast<size_t>(index)].c_str(), isSelected)) {
                                    (void)setEditorUiValue(
                                        state,
                                        row.target,
                                        row.property,
                                        row.enumLabels[static_cast<size_t>(index)]);
                                }
                                if (isSelected) {
                                    ImGui::SetItemDefaultFocus();
                                }
                            }
                            ImGui::EndCombo();
                        }
                        return false;
                    }

                    std::array<char, 512> buffer{};
                    std::snprintf(buffer.data(), buffer.size(), "%s", current.c_str());
                    if (ImGui::InputText(id.c_str(), buffer.data(), buffer.size(), ImGuiInputTextFlags_EnterReturnsTrue)) {
                        (void)setEditorUiValue(state, row.target, row.property, std::string{buffer.data()});
                        return true;
                    }
                    return false;
                }
                case Engine::ReflectedValueType::Vec2: {
                    glm::vec2 value = std::get<glm::vec2>(row.currentValue);
                    float raw[2] = {value.x, value.y};
                    if (ImGui::InputFloat2(id.c_str(), raw)) {
                        (void)setEditorUiValue(state, row.target, row.property, glm::vec2{raw[0], raw[1]});
                        return true;
                    }
                    return false;
                }
                case Engine::ReflectedValueType::Vec3: {
                    glm::vec3 value = std::get<glm::vec3>(row.currentValue);
                    float raw[3] = {value.x, value.y, value.z};
                    if (ImGui::InputFloat3(id.c_str(), raw)) {
                        (void)setEditorUiValue(state, row.target, row.property, glm::vec3{raw[0], raw[1], raw[2]});
                        return true;
                    }
                    return false;
                }
                default:
                    ImGui::TextUnformatted(valueText(row.currentValue).c_str());
                    return false;
            }
        }

        void drawPropertyRows(EditorUiState& state, const std::vector<EditorUiPropertyRow>& rows)
        {
            std::string currentObject;
            for (const EditorUiPropertyRow& row : rows) {
                if (!row.visible || row.category != state.selectedCategory) {
                    continue;
                }
                if (!containsInsensitive(rowSearchText(row), state.searchText)) {
                    continue;
                }

                if (row.objectDisplayName != currentObject) {
                    currentObject = row.objectDisplayName;
                    ImGui::SeparatorText(currentObject.c_str());
                }

                const Engine::ReflectedObjectDescriptor* object =
                    state.reflectionRegistry.object(static_cast<uint32_t>(row.target.object));
                const Engine::ReflectedPropertyDescriptor* descriptor = object
                    ? state.reflectionRegistry.property(object->id, static_cast<uint32_t>(row.property))
                    : nullptr;
                if (!descriptor) {
                    ImGui::Text("%s: missing descriptor", row.displayName.c_str());
                    continue;
                }

                drawValueWidget(state, row, *descriptor);
                if (!row.units.empty()) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", row.units.c_str());
                }
                if (row.dirty) {
                    ImGui::SameLine();
                    ImGui::TextColored(
                        row.requiresExplicitApply ? ImVec4{1.0f, 0.74f, 0.2f, 1.0f} : ImVec4{0.45f, 0.85f, 1.0f, 1.0f},
                        row.requiresExplicitApply ? "requires rebuild/apply" : "pending");
                }
                if (!descriptor->documentation.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("%s", descriptor->documentation.c_str());
                }
                if (!row.diagnostic.empty()) {
                    ImGui::TextDisabled("%s", row.diagnostic.c_str());
                }
            }
        }

        void drawDomainLine(EditorRebuildDomain domains, EditorRebuildDomain domain)
        {
            if (hasEditorRebuildDomain(domains, domain)) {
                ImGui::BulletText("%s", editorRebuildDomainName(domain));
            }
        }

        void drawRebuildButton(
            EditorUiState& state,
            const std::vector<EditorUiPropertyRow>& rows,
            EditorRebuildCommand command)
        {
            (void)rows;
            if (ImGui::Button(editorRebuildCommandName(command))) {
                (void)runEditorUiRebuildCommand(state, command);
            }
        }

#endif
    }

    void initializeEditorUiState(
        EditorUiState& state,
        const EditorProjectSettings& settings,
        const std::filesystem::path& profilePath,
        std::string profileStatus,
        EditorProjectSettingsValidationResult validation)
    {
        state.settings = settings;
        state.baselineSettings = settings;
        state.profilePath = profilePath;
        state.profileStatus = std::move(profileStatus);
        state.validation = std::move(validation);
        state.reflectionRegistry = {};
        registerEditorSettingsReflectionDescriptors(state.reflectionRegistry);
        state.rebuildCoordinator = {};
        state.liveValidation = state.validation;
        state.lastLiveApplyMessage.clear();
        state.selectedCategory = "Project";
        state.searchText.clear();
        state.showAdvanced = false;
        state.lastEditResult = {};
        state.initialized = true;
    }

    std::vector<std::string> editorUiCategories(const EditorUiState& state)
    {
        std::vector<std::string> categories;
        for (const Engine::ReflectedObjectDescriptor& object : state.reflectionRegistry.objects()) {
            if (std::find(categories.begin(), categories.end(), object.category) == categories.end()) {
                categories.push_back(object.category);
            }
        }
        return categories;
    }

    std::vector<EditorUiPropertyRow> buildEditorUiPropertyRows(const EditorUiState& state)
    {
        std::vector<EditorUiPropertyRow> rows;
        EditorSettingsReflectionContext currentContext{const_cast<EditorProjectSettings*>(&state.settings)};
        EditorSettingsReflectionContext baselineContext{const_cast<EditorProjectSettings*>(&state.baselineSettings)};

        for (const EditorSettingsReflectionTarget target : enumerateEditorSettingsTargets(state.settings)) {
            const Engine::ReflectedObjectDescriptor* object =
                state.reflectionRegistry.object(static_cast<uint32_t>(target.object));
            if (!object) {
                continue;
            }
            for (const Engine::ReflectedPropertyDescriptor& descriptor : object->properties) {
                if (!Engine::hasFlag(descriptor.flags, Engine::ReflectedPropertyFlag::EditorVisible)) {
                    continue;
                }
                const Engine::ReflectionResult current = getEditorSetting(
                    currentContext,
                    target,
                    static_cast<EditorSettingsReflectedPropertyId>(descriptor.id));
                const Engine::ReflectionResult baseline = getEditorSetting(
                    baselineContext,
                    target,
                    static_cast<EditorSettingsReflectedPropertyId>(descriptor.id));

                EditorUiPropertyRow row;
                row.target = target;
                row.property = static_cast<EditorSettingsReflectedPropertyId>(descriptor.id);
                row.objectName = object->name;
                row.objectDisplayName = object->displayName;
                if (target.object == EditorSettingsReflectedObjectId::TerrainRenderLod) {
                    row.objectDisplayName += " " + std::to_string(target.index);
                }
                row.category = object->category;
                row.propertyName = descriptor.name;
                row.displayName = descriptor.displayName;
                row.units = descriptor.units;
                row.type = descriptor.type;
                row.flags = descriptor.flags;
                row.enumLabels = descriptor.enumLabels;
                row.advanced = Engine::hasFlag(descriptor.flags, Engine::ReflectedPropertyFlag::Advanced);
                row.readOnly = Engine::hasFlag(descriptor.flags, Engine::ReflectedPropertyFlag::ReadOnly);
                row.requiresExplicitApply = Engine::hasFlag(
                    descriptor.flags,
                    Engine::ReflectedPropertyFlag::RequiresExplicitApply);
                row.lightweightPending = false;
                row.visible = state.showAdvanced || !row.advanced;
                row.supported = supportedType(descriptor.type);

                if (current.status == Engine::ReflectionStatus::Success) {
                    row.currentValue = current.value;
                } else {
                    row.supported = false;
                    row.diagnostic = "Read failed: " + reflectionStatusName(current.status);
                    if (!current.message.empty()) {
                        row.diagnostic += " - " + current.message;
                    }
                }

                if (baseline.status == Engine::ReflectionStatus::Success) {
                    row.baselineValue = baseline.value;
                    row.dirty = current.status == Engine::ReflectionStatus::Success &&
                        !valuesEqual(row.currentValue, row.baselineValue);
                } else {
                    row.diagnostic = "Baseline read failed: " + reflectionStatusName(baseline.status);
                }
                row.lightweightPending = row.dirty && !row.requiresExplicitApply;
                if (!row.supported && row.diagnostic.empty()) {
                    row.diagnostic = "Unsupported reflected value type for generated editing.";
                }
                rows.push_back(std::move(row));
            }
        }
        return rows;
    }

    EditorUiDirtySummary editorUiDirtySummary(const std::vector<EditorUiPropertyRow>& rows)
    {
        EditorUiDirtySummary summary;
        for (const EditorUiPropertyRow& row : rows) {
            if (!row.dirty) {
                continue;
            }
            ++summary.dirtyPropertyCount;
            if (row.requiresExplicitApply) {
                ++summary.rebuildRequiredCount;
            } else {
                ++summary.lightweightPendingCount;
            }
        }
        return summary;
    }

    Engine::ReflectionResult setEditorUiValue(
        EditorUiState& state,
        EditorSettingsReflectionTarget target,
        EditorSettingsReflectedPropertyId property,
        const Engine::ReflectedValue& value)
    {
        EditorSettingsReflectionContext context{&state.settings};
        state.lastEditResult = setEditorSetting(context, target, property, value);
        if (state.lastEditResult.status == Engine::ReflectionStatus::Success) {
            state.validation = validateEditorProjectSettings(state.settings);
        }
        EditorToolEvent event;
        event.type = EditorToolEventType::SettingsChanged;
        event.success = state.lastEditResult.status == Engine::ReflectionStatus::Success;
        event.message = state.lastEditResult.message;
        event.target = target;
        event.property = property;
        emitEditorToolEvent(state, nullptr, event);
        return state.lastEditResult;
    }

    Engine::OpenWorldStreamingBuildResult runEditorUiRebuildCommand(
        EditorUiState& state,
        EditorRebuildCommand command)
    {
        const std::vector<EditorUiPropertyRow> rows = buildEditorUiPropertyRows(state);
        const Engine::OpenWorldStreamingBuildResult result = runEditorSavedBuildRebuild(
            state.rebuildCoordinator,
            state.settings,
            state.baselineSettings,
            rows,
            command);
        if (result.success) {
            advanceRebuildBaseline(state);
        }
        EditorToolEvent event;
        event.type = EditorToolEventType::RebuildCompleted;
        event.success = result.success;
        event.message = result.message;
        event.command = command;
        emitEditorToolEvent(state, nullptr, event);
        return result;
    }

    bool applyEditorUiLightweightRuntimeSettings(EditorUiState& state)
    {
        return applyEditorUiLightweightRuntimeSettings(state, nullptr);
    }

    bool applyEditorUiLightweightRuntimeSettings(EditorUiState& state, EditorLiveApplyHost* host)
    {
        const std::vector<EditorUiPropertyRow> rows = buildEditorUiPropertyRows(state);
        state.rebuildCoordinator.lastCommand = EditorRebuildCommand::ApplyLightweightRuntime;
        state.rebuildCoordinator.status = EditorRebuildCommandStatus::Running;
        state.rebuildCoordinator.lastDirtyState =
            computeEditorRebuildDirtyState(state.settings, state.baselineSettings, rows);
        state.rebuildCoordinator.lastElapsedMs = 0.0f;
        state.rebuildCoordinator.lastBuildResult = {};
        state.rebuildCoordinator.diagnostics.clear();

        state.liveValidation = validateEditorLiveApplySettings(state.settings, false);
        state.validation = state.liveValidation;
        if (!state.liveValidation.valid) {
            state.rebuildCoordinator.status = EditorRebuildCommandStatus::Failed;
            appendValidationDiagnostics(state.rebuildCoordinator, state.liveValidation);
            state.lastLiveApplyMessage = "Lightweight runtime apply rejected invalid editor profile.";
            emitCommandEvent(
                state,
                host,
                EditorToolEventType::LightweightApplyCompleted,
                false,
                state.lastLiveApplyMessage,
                EditorRebuildCommand::ApplyLightweightRuntime);
            return false;
        }
        if (!host || !host->applyLightweightRuntime) {
            state.rebuildCoordinator.status = EditorRebuildCommandStatus::Failed;
            state.rebuildCoordinator.diagnostics.push_back("No live editor runtime is available for lightweight apply.");
            state.lastLiveApplyMessage = state.rebuildCoordinator.diagnostics.back();
            emitCommandEvent(
                state,
                host,
                EditorToolEventType::LightweightApplyCompleted,
                false,
                state.lastLiveApplyMessage,
                EditorRebuildCommand::ApplyLightweightRuntime);
            return false;
        }

        const auto start = std::chrono::steady_clock::now();
        std::string message;
        const bool applied = host->applyLightweightRuntime(host->user, state.settings, message);
        const auto end = std::chrono::steady_clock::now();
        state.rebuildCoordinator.lastElapsedMs =
            std::chrono::duration<float, std::milli>(end - start).count();
        state.rebuildCoordinator.status = applied
            ? EditorRebuildCommandStatus::Succeeded
            : EditorRebuildCommandStatus::Failed;
        if (!message.empty()) {
            state.rebuildCoordinator.diagnostics.push_back(message);
            state.lastLiveApplyMessage = message;
        }
        if (applied) {
            (void)applyEditorLightweightRuntimeBaseline(
                state.rebuildCoordinator,
                state.settings,
                state.baselineSettings,
                rows);
            advanceLightweightBaseline(state);
        }
        emitCommandEvent(
            state,
            host,
            EditorToolEventType::LightweightApplyCompleted,
            applied,
            state.lastLiveApplyMessage,
            EditorRebuildCommand::ApplyLightweightRuntime);
        return applied;
    }

    bool reloadEditorUiStreamingRuntime(EditorUiState& state, EditorLiveApplyHost* host)
    {
        const std::vector<EditorUiPropertyRow> rows = buildEditorUiPropertyRows(state);
        state.rebuildCoordinator.lastCommand = EditorRebuildCommand::ReloadStreamingRuntime;
        state.rebuildCoordinator.status = EditorRebuildCommandStatus::Running;
        state.rebuildCoordinator.lastDirtyState =
            computeEditorRebuildDirtyState(state.settings, state.baselineSettings, rows);
        state.rebuildCoordinator.lastElapsedMs = 0.0f;
        state.rebuildCoordinator.diagnostics.clear();

        state.liveValidation = validateEditorLiveApplySettings(state.settings, true);
        state.validation = state.liveValidation;
        if (!state.liveValidation.valid) {
            state.rebuildCoordinator.status = EditorRebuildCommandStatus::Failed;
            appendValidationDiagnostics(state.rebuildCoordinator, state.liveValidation);
            state.lastLiveApplyMessage = "Streaming reload rejected invalid editor profile.";
            emitCommandEvent(
                state,
                host,
                EditorToolEventType::StreamingReloadCompleted,
                false,
                state.lastLiveApplyMessage,
                EditorRebuildCommand::ReloadStreamingRuntime);
            return false;
        }
        if (!host || !host->reloadStreamingRuntime) {
            state.rebuildCoordinator.status = EditorRebuildCommandStatus::Failed;
            state.rebuildCoordinator.diagnostics.push_back("No live editor runtime is available for streaming reload.");
            state.lastLiveApplyMessage = state.rebuildCoordinator.diagnostics.back();
            emitCommandEvent(
                state,
                host,
                EditorToolEventType::StreamingReloadCompleted,
                false,
                state.lastLiveApplyMessage,
                EditorRebuildCommand::ReloadStreamingRuntime);
            return false;
        }

        const auto start = std::chrono::steady_clock::now();
        std::string message;
        Engine::OpenWorldStreamingBuildResult result;
        const bool reloaded = host->reloadStreamingRuntime(host->user, state.settings, message, result);
        const auto end = std::chrono::steady_clock::now();
        state.rebuildCoordinator.lastElapsedMs =
            std::chrono::duration<float, std::milli>(end - start).count();
        state.rebuildCoordinator.lastBuildResult = result;
        state.rebuildCoordinator.status = reloaded
            ? EditorRebuildCommandStatus::Succeeded
            : EditorRebuildCommandStatus::Failed;
        if (!message.empty()) {
            state.rebuildCoordinator.diagnostics.push_back(message);
            state.lastLiveApplyMessage = message;
        }
        if (!result.message.empty() && result.message != message) {
            state.rebuildCoordinator.diagnostics.push_back(result.message);
        }
        if (reloaded) {
            state.rebuildCoordinator.runtimeReloadRequired = false;
            state.rebuildCoordinator.activeSavedBuildFingerprint =
                Engine::openWorldStreamingRuntimeFingerprint(
                    streamingRuntimeSettingsFromEditorProject(state.settings));
        }
        emitCommandEvent(
            state,
            host,
            EditorToolEventType::StreamingReloadCompleted,
            reloaded,
            state.lastLiveApplyMessage,
            EditorRebuildCommand::ReloadStreamingRuntime);
        return reloaded;
    }

    EditorProjectSettingsValidationResult validateEditorUiCurrentProfile(
        EditorUiState& state,
        bool requireSavedBuildManifest)
    {
        state.rebuildCoordinator.lastCommand = EditorRebuildCommand::ValidateCurrentProfile;
        state.liveValidation = validateEditorLiveApplySettings(state.settings, requireSavedBuildManifest);
        state.validation = state.liveValidation;
        state.rebuildCoordinator.status = state.liveValidation.valid
            ? EditorRebuildCommandStatus::Succeeded
            : EditorRebuildCommandStatus::Failed;
        state.rebuildCoordinator.lastElapsedMs = 0.0f;
        appendValidationDiagnostics(state.rebuildCoordinator, state.liveValidation);
        emitCommandEvent(
            state,
            nullptr,
            EditorToolEventType::ValidationCompleted,
            state.liveValidation.valid,
            state.liveValidation.valid ? "validation succeeded" : "validation failed",
            EditorRebuildCommand::ValidateCurrentProfile);
        return state.liveValidation;
    }

    EditorProjectSettingsValidationResult validateEditorLiveApplySettings(
        const EditorProjectSettings& settings,
        bool requireSavedBuildManifest)
    {
        EditorProjectSettingsValidationResult validation = validateEditorProjectSettings(settings);
        const std::filesystem::path heightmap = settings.streaming.bake.heightmap.sourcePath;
        if (heightmap.empty()) {
            validation.errors.push_back("heightmap source path is empty.");
        } else if (!std::filesystem::is_regular_file(heightmap)) {
            validation.errors.push_back("heightmap source path does not exist: " + heightmap.generic_string());
        }

        if (settings.streaming.bake.renderLods.empty()) {
            validation.errors.push_back("at least one render LOD is required for live reload.");
        }
        for (const Engine::TerrainLodMeshBuildSettings& lod : settings.streaming.bake.renderLods) {
            if (lod.renderResolution < 2) {
                validation.errors.push_back("render LOD resolution must be at least 2.");
            }
        }
        if (settings.streaming.bake.navigationResolution < 2) {
            validation.errors.push_back("terrain navigation resolution must be at least 2.");
        }
        if (settings.streaming.bake.physicsColliderResolution < 2) {
            validation.errors.push_back("physics collider resolution must be at least 2.");
        }
        if (settings.streaming.bake.terrainCache.rootPath.empty()) {
            validation.errors.push_back("terrain cache root is empty.");
        }
        if (settings.streaming.bake.navigationCache.rootPath.empty()) {
            validation.errors.push_back("navigation cache root is empty.");
        }

        if (requireSavedBuildManifest) {
            const std::filesystem::path manifest = settings.streaming.savedBuildManifestPath;
            if (manifest.empty()) {
                validation.errors.push_back("saved build manifest path is empty.");
            } else if (!std::filesystem::is_regular_file(manifest)) {
                validation.errors.push_back("saved build manifest does not exist: " + manifest.generic_string());
            } else {
                Engine::OpenWorldStreamingRuntimeSettings runtimeSettings =
                    streamingRuntimeSettingsFromEditorProject(settings);
                runtimeSettings.rebuildWhenStale = false;
                Engine::OpenWorldStreamingRuntime probe{runtimeSettings};
                const Engine::OpenWorldStreamingBuildResult build = probe.initializeFromSavedBuild();
                if (!build.success) {
                    validation.errors.push_back(
                        "saved build manifest is missing, stale, or invalid: " +
                        (!build.message.empty() ? build.message : build.reason));
                }
                probe.shutdown();
            }
        } else if (!settings.streaming.savedBuildManifestPath.empty() &&
            !std::filesystem::is_regular_file(settings.streaming.savedBuildManifestPath)) {
            validation.warnings.push_back(
                "saved build manifest is missing; reload requires a successful rebuild first.");
        }

        validation.valid = validation.errors.empty();
        return validation;
    }

    void showEditorUi(EditorUiState& state, EditorLiveApplyHost* host)
    {
#if MANUAL_ENGINE_ENABLE_DEBUG_TOOLS
        if (!state.initialized) {
            return;
        }

        std::vector<EditorUiPropertyRow> rows = buildEditorUiPropertyRows(state);
        const EditorUiDirtySummary dirty = editorUiDirtySummary(rows);
        state.rebuildCoordinator.lastDirtyState =
            computeEditorRebuildDirtyState(state.settings, state.baselineSettings, rows);

        ImGui::Begin("ManualEngine Editor");
        ImGui::Text("Profile: %s", state.profilePath.empty() ? "<none>" : state.profilePath.generic_string().c_str());
        if (!state.profileStatus.empty()) {
            ImGui::TextWrapped("%s", state.profileStatus.c_str());
        }
        ImGui::Text(
            "Dirty: %u | rebuild/apply: %u | lightweight pending: %u",
            dirty.dirtyPropertyCount,
            dirty.rebuildRequiredCount,
            dirty.lightweightPendingCount);
        ImGui::Separator();

        if (ImGui::BeginTabBar("editor_tabs")) {
            if (ImGui::BeginTabItem("Project Settings")) {
                std::array<char, 128> search{};
                std::snprintf(search.data(), search.size(), "%s", state.searchText.c_str());
                if (ImGui::InputText("Search", search.data(), search.size())) {
                    state.searchText = search.data();
                }
                ImGui::Checkbox("Show advanced", &state.showAdvanced);

                const std::vector<std::string> categories = editorUiCategories(state);
                ImGui::BeginChild("editor_categories", ImVec2{220.0f, 0.0f}, true);
                for (const std::string& category : categories) {
                    if (ImGui::Selectable(category.c_str(), state.selectedCategory == category)) {
                        state.selectedCategory = category;
                    }
                }
                ImGui::EndChild();
                ImGui::SameLine();
                ImGui::BeginChild("editor_properties", ImVec2{0.0f, 0.0f}, true);
                drawPropertyRows(state, rows);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Rebuild")) {
                const EditorRebuildDirtyState rebuildDirty = state.rebuildCoordinator.lastDirtyState;
                ImGui::TextWrapped("Rebuild commands write saved derived data and manifests. Use reload to swap rebuilt streaming data into this editor session.");
                ImGui::Text(
                    "Saved build fingerprint: %s",
                    rebuildDirty.savedBuildFingerprintDirty ? "dirty" : "current");
                if (state.rebuildCoordinator.runtimeReloadRequired) {
                    ImGui::TextColored(ImVec4{1.0f, 0.74f, 0.2f, 1.0f}, "Runtime reload required to see rebuilt outputs.");
                }
                ImGui::SeparatorText("Dirty Domains");
                drawDomainLine(rebuildDirty.domains, EditorRebuildDomain::Terrain);
                drawDomainLine(rebuildDirty.domains, EditorRebuildDomain::RenderLods);
                drawDomainLine(rebuildDirty.domains, EditorRebuildDomain::Navigation);
                drawDomainLine(rebuildDirty.domains, EditorRebuildDomain::SceneGeometry);
                drawDomainLine(rebuildDirty.domains, EditorRebuildDomain::PhysicsColliders);
                drawDomainLine(rebuildDirty.domains, EditorRebuildDomain::StreamingSavedBuild);
                drawDomainLine(rebuildDirty.domains, EditorRebuildDomain::LightweightRuntime);
                if (rebuildDirty.domains == EditorRebuildDomain::None) {
                    ImGui::TextUnformatted("No rebuild domains are dirty.");
                }
                ImGui::SeparatorText("Commands");
                drawRebuildButton(state, rows, EditorRebuildCommand::TerrainChunks);
                ImGui::SameLine();
                drawRebuildButton(state, rows, EditorRebuildCommand::RenderLods);
                drawRebuildButton(state, rows, EditorRebuildCommand::Navigation);
                ImGui::SameLine();
                drawRebuildButton(state, rows, EditorRebuildCommand::PhysicsColliders);
                drawRebuildButton(state, rows, EditorRebuildCommand::FullSavedBuild);
                ImGui::SameLine();
                if (ImGui::Button(editorRebuildCommandName(EditorRebuildCommand::ApplyLightweightRuntime))) {
                    (void)applyEditorUiLightweightRuntimeSettings(state, host);
                }
                ImGui::SameLine();
                if (ImGui::Button(editorRebuildCommandName(EditorRebuildCommand::ReloadStreamingRuntime))) {
                    (void)reloadEditorUiStreamingRuntime(state, host);
                }
                ImGui::SeparatorText("Changed Rebuild Settings");
                ImGui::Separator();
                for (const EditorUiPropertyRow& row : rows) {
                    if (row.dirty && row.requiresExplicitApply) {
                        ImGui::Text(
                            "%s / %s: %s -> %s",
                            row.category.c_str(),
                            row.displayName.c_str(),
                            valueText(row.baselineValue).c_str(),
                            valueText(row.currentValue).c_str());
                    }
                }
                if (dirty.rebuildRequiredCount == 0) {
                    ImGui::TextUnformatted("No rebuild-affecting settings changed.");
                }
                ImGui::SeparatorText("Last Command");
                ImGui::Text("Command: %s", editorRebuildCommandName(state.rebuildCoordinator.lastCommand));
                ImGui::Text("Status: %s",
                    state.rebuildCoordinator.status == EditorRebuildCommandStatus::Idle ? "Idle" :
                    state.rebuildCoordinator.status == EditorRebuildCommandStatus::Running ? "Running" :
                    state.rebuildCoordinator.status == EditorRebuildCommandStatus::Succeeded ? "Succeeded" : "Failed");
                ImGui::Text("Elapsed: %.3f ms", state.rebuildCoordinator.lastElapsedMs);
                if (!state.rebuildCoordinator.lastBuildResult.fingerprint.empty()) {
                    ImGui::Text("Fingerprint: %s", state.rebuildCoordinator.lastBuildResult.fingerprint.c_str());
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Diagnostics")) {
                const Engine::ReflectionDiagnostics reflection = state.reflectionRegistry.diagnostics();
                ImGui::Text(
                    "Reflection objects/properties: %u / %u",
                    reflection.objectCount,
                    reflection.propertyCount);
                if (!reflection.lastMessage.empty()) {
                    ImGui::TextWrapped("%s", reflection.lastMessage.c_str());
                }
                ImGui::Separator();
                ImGui::Text("Validation: %s", state.validation.valid ? "valid" : "invalid");
                for (const std::string& error : state.validation.errors) {
                    ImGui::TextColored(ImVec4{1.0f, 0.35f, 0.35f, 1.0f}, "Error: %s", error.c_str());
                }
                for (const std::string& warning : state.validation.warnings) {
                    ImGui::TextColored(ImVec4{1.0f, 0.78f, 0.25f, 1.0f}, "Warning: %s", warning.c_str());
                }
                if (state.lastEditResult.status != Engine::ReflectionStatus::Success ||
                    !state.lastEditResult.message.empty()) {
                    ImGui::Separator();
                    ImGui::Text("Last edit: %s", reflectionStatusName(state.lastEditResult.status).c_str());
                    if (!state.lastEditResult.message.empty()) {
                        ImGui::TextWrapped("%s", state.lastEditResult.message.c_str());
                    }
                }
                if (!state.rebuildCoordinator.diagnostics.empty()) {
                    ImGui::Separator();
                    ImGui::TextUnformatted("Editor command diagnostics");
                    for (const std::string& message : state.rebuildCoordinator.diagnostics) {
                        ImGui::TextWrapped("%s", message.c_str());
                    }
                }
                if (!state.lastLiveApplyMessage.empty()) {
                    ImGui::Separator();
                    ImGui::TextWrapped("%s", state.lastLiveApplyMessage.c_str());
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Runtime/Viewport Settings")) {
                ImGui::TextWrapped("These lightweight settings apply to the running editor viewport only when explicitly applied.");
                if (ImGui::Button(editorRebuildCommandName(EditorRebuildCommand::ApplyLightweightRuntime))) {
                    (void)applyEditorUiLightweightRuntimeSettings(state, host);
                }
                ImGui::SameLine();
                if (ImGui::Button(editorRebuildCommandName(EditorRebuildCommand::ValidateCurrentProfile))) {
                    (void)validateEditorUiCurrentProfile(state, false);
                }
                ImGui::Separator();
                for (const char* category : {"Renderer", "Debug Draw", "Camera"}) {
                    if (ImGui::CollapsingHeader(category, ImGuiTreeNodeFlags_DefaultOpen)) {
                        const std::string previousCategory = state.selectedCategory;
                        state.selectedCategory = category;
                        drawPropertyRows(state, rows);
                        state.selectedCategory = previousCategory;
                    }
                }
                ImGui::EndTabItem();
            }
            if (state.toolScripts && ImGui::BeginTabItem("Tool Scripts")) {
                showEditorToolScriptsPanel(*state.toolScripts, state, host);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
#else
        (void)state;
#endif
    }

}
