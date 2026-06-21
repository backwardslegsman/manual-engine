#include "App/EditorToolScripting.hpp"

#include "App/EditorUi.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string_view>
#include <optional>
#include <system_error>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#ifndef MANUAL_ENGINE_ENABLE_DEBUG_TOOLS
#define MANUAL_ENGINE_ENABLE_DEBUG_TOOLS 1
#endif

#if MANUAL_ENGINE_ENABLE_DEBUG_TOOLS
#include <imgui.h>
#endif

namespace ManualEngine::App {
    namespace {
        struct LuaInvocation {
            EditorToolScripts* scripts = nullptr;
            EditorToolScriptRecord* record = nullptr;
            EditorToolContext context;
        };

        [[nodiscard]] std::string lower(std::string_view value)
        {
            std::string out;
            out.reserve(value.size());
            for (char c : value) {
                out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            return out;
        }

        [[nodiscard]] std::string normalizeName(std::string_view value)
        {
            std::string out;
            for (char c : value) {
                if (std::isalnum(static_cast<unsigned char>(c))) {
                    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                }
            }
            return out;
        }

        void appendDiagnostic(EditorToolScripts& scripts, std::string message)
        {
            scripts.diagnostics.push_back(std::move(message));
            if (scripts.diagnostics.size() > 64) {
                scripts.diagnostics.erase(scripts.diagnostics.begin());
            }
        }

        [[nodiscard]] LuaInvocation* invocation(lua_State* state)
        {
            return static_cast<LuaInvocation*>(lua_touserdata(state, lua_upvalueindex(1)));
        }

        [[nodiscard]] std::string luaString(lua_State* state, int index)
        {
            size_t length = 0;
            const char* value = lua_tolstring(state, index, &length);
            return value ? std::string{value, length} : std::string{};
        }

        [[nodiscard]] const Engine::ReflectedObjectDescriptor* descriptorForTarget(
            const Engine::ReflectionRegistry& registry,
            EditorSettingsReflectionTarget target)
        {
            return registry.object(static_cast<uint32_t>(target.object));
        }

        [[nodiscard]] const Engine::ReflectedPropertyDescriptor* descriptorForProperty(
            const Engine::ReflectionRegistry& registry,
            EditorSettingsReflectionTarget target,
            EditorSettingsReflectedPropertyId property)
        {
            const Engine::ReflectedObjectDescriptor* object = descriptorForTarget(registry, target);
            return object ? registry.property(object->id, static_cast<uint32_t>(property)) : nullptr;
        }

        [[nodiscard]] std::optional<EditorSettingsReflectedObjectId> objectIdFromString(
            const Engine::ReflectionRegistry& registry,
            std::string_view value)
        {
            const std::string wanted = normalizeName(value);
            for (const Engine::ReflectedObjectDescriptor& object : registry.objects()) {
                if (normalizeName(object.name) == wanted ||
                    normalizeName(object.displayName) == wanted ||
                    normalizeName(object.category) == wanted) {
                    return static_cast<EditorSettingsReflectedObjectId>(object.id);
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<EditorSettingsReflectedPropertyId> propertyIdFromString(
            const Engine::ReflectionRegistry& registry,
            EditorSettingsReflectionTarget target,
            std::string_view value)
        {
            const Engine::ReflectedObjectDescriptor* object = descriptorForTarget(registry, target);
            if (!object) {
                return std::nullopt;
            }
            const std::string wanted = normalizeName(value);
            for (const Engine::ReflectedPropertyDescriptor& property : object->properties) {
                if (normalizeName(property.name) == wanted ||
                    normalizeName(property.displayName) == wanted) {
                    return static_cast<EditorSettingsReflectedPropertyId>(property.id);
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<EditorSettingsReflectionTarget> readTarget(
            lua_State* state,
            int index,
            const Engine::ReflectionRegistry& registry)
        {
            if (!lua_istable(state, index)) {
                return std::nullopt;
            }
            EditorSettingsReflectionTarget target;
            lua_getfield(state, index, "object");
            if (lua_isinteger(state, -1)) {
                target.object = static_cast<EditorSettingsReflectedObjectId>(
                    static_cast<uint32_t>(lua_tointeger(state, -1)));
            } else if (lua_isstring(state, -1)) {
                const std::optional<EditorSettingsReflectedObjectId> object =
                    objectIdFromString(registry, luaString(state, -1));
                if (!object) {
                    lua_pop(state, 1);
                    return std::nullopt;
                }
                target.object = *object;
            } else {
                lua_pop(state, 1);
                return std::nullopt;
            }
            lua_pop(state, 1);

            lua_getfield(state, index, "index");
            if (lua_isinteger(state, -1)) {
                target.index = static_cast<uint32_t>(std::max<lua_Integer>(lua_tointeger(state, -1), 0));
            }
            lua_pop(state, 1);
            return target;
        }

        [[nodiscard]] std::optional<EditorSettingsReflectedPropertyId> readProperty(
            lua_State* state,
            int index,
            const Engine::ReflectionRegistry& registry,
            EditorSettingsReflectionTarget target)
        {
            if (lua_isinteger(state, index)) {
                return static_cast<EditorSettingsReflectedPropertyId>(
                    static_cast<uint32_t>(lua_tointeger(state, index)));
            }
            if (lua_isstring(state, index)) {
                return propertyIdFromString(registry, target, luaString(state, index));
            }
            return std::nullopt;
        }

        void pushValue(lua_State* state, const Engine::ReflectedValue& value)
        {
            if (const auto* v = std::get_if<bool>(&value)) {
                lua_pushboolean(state, *v);
            } else if (const auto* v = std::get_if<int64_t>(&value)) {
                lua_pushinteger(state, static_cast<lua_Integer>(*v));
            } else if (const auto* v = std::get_if<uint64_t>(&value)) {
                lua_pushinteger(state, static_cast<lua_Integer>(*v));
            } else if (const auto* v = std::get_if<float>(&value)) {
                lua_pushnumber(state, static_cast<lua_Number>(*v));
            } else if (const auto* v = std::get_if<std::string>(&value)) {
                lua_pushlstring(state, v->c_str(), v->size());
            } else if (const auto* v = std::get_if<glm::vec2>(&value)) {
                lua_createtable(state, 2, 2);
                lua_pushnumber(state, v->x);
                lua_setfield(state, -2, "x");
                lua_pushnumber(state, v->y);
                lua_setfield(state, -2, "y");
                lua_pushnumber(state, v->x);
                lua_rawseti(state, -2, 1);
                lua_pushnumber(state, v->y);
                lua_rawseti(state, -2, 2);
            } else if (const auto* v = std::get_if<glm::vec3>(&value)) {
                lua_createtable(state, 3, 3);
                lua_pushnumber(state, v->x);
                lua_setfield(state, -2, "x");
                lua_pushnumber(state, v->y);
                lua_setfield(state, -2, "y");
                lua_pushnumber(state, v->z);
                lua_setfield(state, -2, "z");
                lua_pushnumber(state, v->x);
                lua_rawseti(state, -2, 1);
                lua_pushnumber(state, v->y);
                lua_rawseti(state, -2, 2);
                lua_pushnumber(state, v->z);
                lua_rawseti(state, -2, 3);
            } else {
                lua_pushnil(state);
            }
        }

        [[nodiscard]] std::optional<float> readTableNumber(lua_State* state, int index, const char* field, int arrayIndex)
        {
            lua_getfield(state, index, field);
            if (lua_isnumber(state, -1)) {
                const float value = static_cast<float>(lua_tonumber(state, -1));
                lua_pop(state, 1);
                return value;
            }
            lua_pop(state, 1);
            lua_rawgeti(state, index, arrayIndex);
            if (lua_isnumber(state, -1)) {
                const float value = static_cast<float>(lua_tonumber(state, -1));
                lua_pop(state, 1);
                return value;
            }
            lua_pop(state, 1);
            return std::nullopt;
        }

        [[nodiscard]] std::optional<Engine::ReflectedValue> readValue(
            lua_State* state,
            int index,
            Engine::ReflectedValueType type)
        {
            switch (type) {
                case Engine::ReflectedValueType::Bool:
                    if (lua_isboolean(state, index)) return Engine::ReflectedValue{lua_toboolean(state, index) != 0};
                    return std::nullopt;
                case Engine::ReflectedValueType::Int64:
                    if (lua_isinteger(state, index)) return Engine::ReflectedValue{static_cast<int64_t>(lua_tointeger(state, index))};
                    return std::nullopt;
                case Engine::ReflectedValueType::UInt64:
                    if (lua_isinteger(state, index)) return Engine::ReflectedValue{static_cast<uint64_t>(std::max<lua_Integer>(lua_tointeger(state, index), 0))};
                    return std::nullopt;
                case Engine::ReflectedValueType::Float:
                    if (lua_isnumber(state, index)) return Engine::ReflectedValue{static_cast<float>(lua_tonumber(state, index))};
                    return std::nullopt;
                case Engine::ReflectedValueType::String:
                    if (lua_isstring(state, index)) return Engine::ReflectedValue{luaString(state, index)};
                    return std::nullopt;
                case Engine::ReflectedValueType::Vec2: {
                    if (!lua_istable(state, index)) return std::nullopt;
                    const std::optional<float> x = readTableNumber(state, index, "x", 1);
                    const std::optional<float> y = readTableNumber(state, index, "y", 2);
                    if (!x || !y) return std::nullopt;
                    return Engine::ReflectedValue{glm::vec2{*x, *y}};
                }
                case Engine::ReflectedValueType::Vec3: {
                    if (!lua_istable(state, index)) return std::nullopt;
                    const std::optional<float> x = readTableNumber(state, index, "x", 1);
                    const std::optional<float> y = readTableNumber(state, index, "y", 2);
                    const std::optional<float> z = readTableNumber(state, index, "z", 3);
                    if (!x || !y || !z) return std::nullopt;
                    return Engine::ReflectedValue{glm::vec3{*x, *y, *z}};
                }
                default:
                    return std::nullopt;
            }
        }

        void pushStatusResult(lua_State* state, bool success, std::string_view message = {})
        {
            lua_pushboolean(state, success);
            if (!message.empty()) {
                lua_pushlstring(state, message.data(), message.size());
            } else {
                lua_pushnil(state);
            }
        }

        int luaTargets(lua_State* state)
        {
            LuaInvocation* invoke = invocation(state);
            if (!invoke || !invoke->context.valid()) {
                lua_newtable(state);
                return 1;
            }
            lua_newtable(state);
            int row = 1;
            for (EditorSettingsReflectionTarget target : enumerateEditorSettingsTargets(invoke->context.settings())) {
                const Engine::ReflectedObjectDescriptor* object =
                    descriptorForTarget(invoke->context.reflectionRegistry(), target);
                if (!object) {
                    continue;
                }
                lua_newtable(state);
                lua_pushinteger(state, object->id);
                lua_setfield(state, -2, "object_id");
                lua_pushlstring(state, object->name.c_str(), object->name.size());
                lua_setfield(state, -2, "object");
                lua_pushlstring(state, object->displayName.c_str(), object->displayName.size());
                lua_setfield(state, -2, "display_name");
                lua_pushinteger(state, target.index);
                lua_setfield(state, -2, "index");
                lua_rawseti(state, -2, row++);
            }
            return 1;
        }

        int luaGet(lua_State* state)
        {
            LuaInvocation* invoke = invocation(state);
            if (!invoke || !invoke->context.valid()) {
                lua_pushnil(state);
                lua_pushliteral(state, "no editor context");
                return 2;
            }
            const std::optional<EditorSettingsReflectionTarget> target =
                readTarget(state, 1, invoke->context.reflectionRegistry());
            if (!target) {
                lua_pushnil(state);
                lua_pushliteral(state, "invalid target");
                return 2;
            }
            const std::optional<EditorSettingsReflectedPropertyId> property =
                readProperty(state, 2, invoke->context.reflectionRegistry(), *target);
            if (!property) {
                lua_pushnil(state);
                lua_pushliteral(state, "invalid property");
                return 2;
            }
            const Engine::ReflectionResult result = invoke->context.getSetting(*target, *property);
            if (result.status != Engine::ReflectionStatus::Success) {
                lua_pushnil(state);
                lua_pushlstring(state, result.message.c_str(), result.message.size());
                return 2;
            }
            pushValue(state, result.value);
            lua_pushnil(state);
            return 2;
        }

        int luaSet(lua_State* state)
        {
            LuaInvocation* invoke = invocation(state);
            if (!invoke || !invoke->context.valid()) {
                pushStatusResult(state, false, "no editor context");
                return 2;
            }
            const std::optional<EditorSettingsReflectionTarget> target =
                readTarget(state, 1, invoke->context.reflectionRegistry());
            if (!target) {
                pushStatusResult(state, false, "invalid target");
                return 2;
            }
            const std::optional<EditorSettingsReflectedPropertyId> property =
                readProperty(state, 2, invoke->context.reflectionRegistry(), *target);
            if (!property) {
                pushStatusResult(state, false, "invalid property");
                return 2;
            }
            const Engine::ReflectedPropertyDescriptor* descriptor =
                descriptorForProperty(invoke->context.reflectionRegistry(), *target, *property);
            if (!descriptor ||
                !Engine::hasFlag(descriptor->flags, Engine::ReflectedPropertyFlag::ScriptVisible)) {
                pushStatusResult(state, false, "property is not script visible");
                return 2;
            }
            const std::optional<Engine::ReflectedValue> value = readValue(state, 3, descriptor->type);
            if (!value) {
                pushStatusResult(state, false, "type mismatch");
                return 2;
            }
            const Engine::ReflectionResult result = invoke->context.setSetting(*target, *property, *value);
            pushStatusResult(
                state,
                result.status == Engine::ReflectionStatus::Success,
                result.message);
            return 2;
        }

        int luaValidate(lua_State* state)
        {
            LuaInvocation* invoke = invocation(state);
            bool requireSavedBuild = false;
            if (lua_istable(state, 1)) {
                lua_getfield(state, 1, "require_saved_build");
                requireSavedBuild = lua_toboolean(state, -1) != 0;
                lua_pop(state, 1);
            }
            const EditorProjectSettingsValidationResult validation =
                invoke->context.validate(requireSavedBuild);
            lua_createtable(state, 0, 3);
            lua_pushboolean(state, validation.valid);
            lua_setfield(state, -2, "valid");
            lua_newtable(state);
            int index = 1;
            for (const std::string& error : validation.errors) {
                lua_pushlstring(state, error.c_str(), error.size());
                lua_rawseti(state, -2, index++);
            }
            lua_setfield(state, -2, "errors");
            lua_newtable(state);
            index = 1;
            for (const std::string& warning : validation.warnings) {
                lua_pushlstring(state, warning.c_str(), warning.size());
                lua_rawseti(state, -2, index++);
            }
            lua_setfield(state, -2, "warnings");
            return 1;
        }

        [[nodiscard]] EditorRebuildCommand rebuildCommandFromString(std::string_view value)
        {
            const std::string normalized = lower(value);
            if (normalized == "terrain") return EditorRebuildCommand::TerrainChunks;
            if (normalized == "render_lods" || normalized == "render_lod") return EditorRebuildCommand::RenderLods;
            if (normalized == "navigation") return EditorRebuildCommand::Navigation;
            if (normalized == "physics") return EditorRebuildCommand::PhysicsColliders;
            return EditorRebuildCommand::FullSavedBuild;
        }

        int luaRebuild(lua_State* state)
        {
            LuaInvocation* invoke = invocation(state);
            const EditorRebuildCommand command = lua_isstring(state, 1)
                ? rebuildCommandFromString(luaString(state, 1))
                : EditorRebuildCommand::FullSavedBuild;
            const Engine::OpenWorldStreamingBuildResult result = invoke->context.rebuild(command);
            lua_createtable(state, 0, 3);
            lua_pushboolean(state, result.success);
            lua_setfield(state, -2, "success");
            lua_pushlstring(state, result.message.c_str(), result.message.size());
            lua_setfield(state, -2, "message");
            lua_pushlstring(state, result.fingerprint.c_str(), result.fingerprint.size());
            lua_setfield(state, -2, "fingerprint");
            return 1;
        }

        int luaApplyLightweight(lua_State* state)
        {
            LuaInvocation* invoke = invocation(state);
            lua_pushboolean(state, invoke->context.applyLightweight());
            return 1;
        }

        int luaReloadStreaming(lua_State* state)
        {
            LuaInvocation* invoke = invocation(state);
            lua_pushboolean(state, invoke->context.reloadStreaming());
            return 1;
        }

        int luaDirty(lua_State* state)
        {
            LuaInvocation* invoke = invocation(state);
            const EditorRebuildDirtyState dirty = invoke->context.dirty();
            lua_createtable(state, 0, 5);
            lua_pushinteger(state, static_cast<lua_Integer>(dirty.rebuildPropertyCount));
            lua_setfield(state, -2, "rebuild_property_count");
            lua_pushinteger(state, static_cast<lua_Integer>(dirty.lightweightPropertyCount));
            lua_setfield(state, -2, "lightweight_property_count");
            lua_pushboolean(state, dirty.savedBuildFingerprintDirty);
            lua_setfield(state, -2, "saved_build_fingerprint_dirty");
            lua_pushlstring(state, dirty.currentFingerprint.c_str(), dirty.currentFingerprint.size());
            lua_setfield(state, -2, "current_fingerprint");
            lua_newtable(state);
            int index = 1;
            for (EditorRebuildDomain domain : {
                     EditorRebuildDomain::Terrain,
                     EditorRebuildDomain::RenderLods,
                     EditorRebuildDomain::Navigation,
                     EditorRebuildDomain::SceneGeometry,
                     EditorRebuildDomain::PhysicsColliders,
                     EditorRebuildDomain::StreamingSavedBuild,
                     EditorRebuildDomain::LightweightRuntime,
                 }) {
                if (hasEditorRebuildDomain(dirty.domains, domain)) {
                    const char* name = editorRebuildDomainName(domain);
                    lua_pushstring(state, name);
                    lua_rawseti(state, -2, index++);
                }
            }
            lua_setfield(state, -2, "domains");
            return 1;
        }

        int luaLog(lua_State* state)
        {
            LuaInvocation* invoke = invocation(state);
            invoke->context.log(luaString(state, 1));
            return 0;
        }

        void bind(lua_State* state, LuaInvocation& invoke, const char* name, lua_CFunction function)
        {
            lua_pushlightuserdata(state, &invoke);
            lua_pushcclosure(state, function, 1);
            lua_setfield(state, -2, name);
        }

        void pushEditorTable(lua_State* state, LuaInvocation& invoke)
        {
            lua_newtable(state);
            lua_newtable(state);
            bind(state, invoke, "targets", luaTargets);
            bind(state, invoke, "get", luaGet);
            bind(state, invoke, "set", luaSet);
            lua_setfield(state, -2, "settings");

            lua_newtable(state);
            bind(state, invoke, "validate", luaValidate);
            bind(state, invoke, "rebuild", luaRebuild);
            bind(state, invoke, "apply_lightweight", luaApplyLightweight);
            bind(state, invoke, "reload_streaming", luaReloadStreaming);
            lua_setfield(state, -2, "commands");

            bind(state, invoke, "dirty", luaDirty);
            bind(state, invoke, "log", luaLog);
            lua_setglobal(state, "editor");
        }

        [[nodiscard]] std::string statusMessage(Engine::ReflectionStatus status)
        {
            switch (status) {
                case Engine::ReflectionStatus::Success: return "success";
                case Engine::ReflectionStatus::NotFound: return "not found";
                case Engine::ReflectionStatus::DuplicateObject: return "duplicate object";
                case Engine::ReflectionStatus::DuplicateProperty: return "duplicate property";
                case Engine::ReflectionStatus::InvalidHandle: return "invalid handle";
                case Engine::ReflectionStatus::WrongHandleKind: return "wrong handle kind";
                case Engine::ReflectionStatus::WrongOwner: return "wrong owner";
                case Engine::ReflectionStatus::UnknownProperty: return "unknown property";
                case Engine::ReflectionStatus::TypeMismatch: return "type mismatch";
                case Engine::ReflectionStatus::ReadOnly: return "read only";
                case Engine::ReflectionStatus::ValidationFailed: return "validation failed";
                case Engine::ReflectionStatus::Unsupported: return "unsupported";
                case Engine::ReflectionStatus::SubsystemRejected: return "subsystem rejected";
            }
            return "unknown";
        }

        void instructionHook(lua_State* state, lua_Debug*)
        {
            int* remaining = *static_cast<int**>(lua_getextraspace(state));
            if (!remaining) {
                return;
            }
            --(*remaining);
            if (*remaining <= 0) {
                luaL_error(state, "editor tool script instruction budget exceeded");
            }
        }
    }

    Engine::ReflectionResult EditorToolContext::getSetting(
        EditorSettingsReflectionTarget target,
        EditorSettingsReflectedPropertyId property) const
    {
        if (!state_) {
            return {Engine::ReflectionStatus::InvalidHandle, "editor tool context is not bound"};
        }
        EditorSettingsReflectionContext context{&state_->settings};
        return getEditorSetting(context, target, property);
    }

    Engine::ReflectionResult EditorToolContext::setSetting(
        EditorSettingsReflectionTarget target,
        EditorSettingsReflectedPropertyId property,
        const Engine::ReflectedValue& value) const
    {
        if (!state_) {
            return {Engine::ReflectionStatus::InvalidHandle, "editor tool context is not bound"};
        }
        const Engine::ReflectedPropertyDescriptor* descriptor =
            descriptorForProperty(state_->reflectionRegistry, target, property);
        if (!descriptor || !Engine::hasFlag(descriptor->flags, Engine::ReflectedPropertyFlag::ScriptVisible)) {
            return {Engine::ReflectionStatus::Unsupported, "property is not script visible"};
        }
        Engine::ReflectionResult result = setEditorUiValue(*state_, target, property, value);
        return result;
    }

    EditorProjectSettingsValidationResult EditorToolContext::validate(bool requireSavedBuildManifest) const
    {
        EditorProjectSettingsValidationResult result;
        if (!state_) {
            result.valid = false;
            result.errors.push_back("editor tool context is not bound");
            return result;
        }
        result = validateEditorUiCurrentProfile(*state_, requireSavedBuildManifest);
        return result;
    }

    Engine::OpenWorldStreamingBuildResult EditorToolContext::rebuild(EditorRebuildCommand command) const
    {
        Engine::OpenWorldStreamingBuildResult result;
        if (!state_) {
            result.success = false;
            result.message = "editor tool context is not bound";
            return result;
        }
        result = runEditorUiRebuildCommand(*state_, command);
        return result;
    }

    bool EditorToolContext::applyLightweight() const
    {
        if (!state_) {
            return false;
        }
        const bool success = applyEditorUiLightweightRuntimeSettings(*state_, liveHost_);
        return success;
    }

    bool EditorToolContext::reloadStreaming() const
    {
        if (!state_) {
            return false;
        }
        const bool success = reloadEditorUiStreamingRuntime(*state_, liveHost_);
        return success;
    }

    EditorRebuildDirtyState EditorToolContext::dirty() const
    {
        if (!state_) {
            return {};
        }
        const std::vector<EditorUiPropertyRow> rows = buildEditorUiPropertyRows(*state_);
        return computeEditorRebuildDirtyState(state_->settings, state_->baselineSettings, rows);
    }

    void EditorToolContext::log(std::string message) const
    {
        if (logs_) {
            logs_->push_back(std::move(message));
        }
    }

    bool EditorToolContext::valid() const
    {
        return state_ != nullptr;
    }

    const Engine::ReflectionRegistry& EditorToolContext::reflectionRegistry() const
    {
        static const Engine::ReflectionRegistry empty;
        return state_ ? state_->reflectionRegistry : empty;
    }

    const EditorProjectSettings& EditorToolContext::settings() const
    {
        static const EditorProjectSettings defaults = defaultEditorProjectSettings();
        return state_ ? state_->settings : defaults;
    }

    void EditorToolContext::setLogSink(std::vector<std::string>* logs)
    {
        logs_ = logs;
    }

    uint32_t EditorToolHooks::addHook(EditorToolHook hook)
    {
        if (!hook) {
            return 0;
        }
        const uint32_t id = nextId_++;
        hooks_.push_back({id, std::move(hook)});
        return id;
    }

    bool EditorToolHooks::removeHook(uint32_t id)
    {
        const auto before = hooks_.size();
        hooks_.erase(
            std::remove_if(hooks_.begin(), hooks_.end(), [id](const HookRecord& record) {
                return record.id == id;
            }),
            hooks_.end());
        return hooks_.size() != before;
    }

    void EditorToolHooks::emit(const EditorToolEvent& event, EditorToolContext& context)
    {
        recentEvents_.push_back(event);
        if (recentEvents_.size() > 128) {
            recentEvents_.erase(recentEvents_.begin());
        }
        for (HookRecord& hook : hooks_) {
            hook.hook(event, context);
        }
    }

    uint32_t EditorToolHooks::hookCount() const
    {
        return static_cast<uint32_t>(hooks_.size());
    }

    std::vector<EditorToolEvent> EditorToolHooks::recentEvents() const
    {
        return recentEvents_;
    }

    void EditorToolHooks::clearEvents()
    {
        recentEvents_.clear();
    }

    EditorToolContext makeEditorToolContext(EditorUiState& state, EditorLiveApplyHost* liveHost)
    {
        EditorToolContext context;
        context.state_ = &state;
        context.liveHost_ = liveHost;
        return context;
    }

    void emitEditorToolEvent(
        EditorUiState& state,
        EditorLiveApplyHost* liveHost,
        const EditorToolEvent& event)
    {
        if (!state.toolHooks) {
            return;
        }
        EditorToolContext context = makeEditorToolContext(state, liveHost);
        state.toolHooks->emit(event, context);
    }

    void rescanEditorToolScripts(EditorToolScripts& scripts)
    {
        scripts.scripts.clear();
        scripts.diagnostics.clear();
        std::error_code error;
        if (!std::filesystem::exists(scripts.scriptDirectory, error)) {
            appendDiagnostic(scripts, "Tool script directory does not exist; no scripts discovered.");
            return;
        }
        if (!std::filesystem::is_directory(scripts.scriptDirectory, error)) {
            appendDiagnostic(scripts, "Tool script path is not a directory.");
            return;
        }
        for (const std::filesystem::directory_entry& entry :
            std::filesystem::directory_iterator(scripts.scriptDirectory, error)) {
            if (entry.is_regular_file(error) && entry.path().extension() == ".lua") {
                EditorToolScriptRecord record;
                record.path = entry.path();
                record.displayName = entry.path().filename().generic_string();
                scripts.scripts.push_back(std::move(record));
            }
        }
        std::sort(scripts.scripts.begin(), scripts.scripts.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.displayName < rhs.displayName;
        });
        if (error) {
            appendDiagnostic(scripts, "Tool script discovery warning: " + error.message());
        }
    }

    bool runEditorToolScript(
        EditorToolScripts& scripts,
        size_t scriptIndex,
        EditorUiState& state,
        EditorLiveApplyHost* liveHost)
    {
        if (scriptIndex >= scripts.scripts.size()) {
            appendDiagnostic(scripts, "Script index is out of range.");
            return false;
        }
        EditorToolScriptRecord& record = scripts.scripts[scriptIndex];
        record.status = EditorToolScriptStatus::Failed;
        record.message.clear();
        record.logs.clear();
        EditorToolEvent started;
        started.type = EditorToolEventType::ScriptStarted;
        started.scriptPath = record.path;
        started.message = record.displayName;
        emitEditorToolEvent(state, liveHost, started);

        int remainingInstructions = static_cast<int>(scripts.instructionBudget);
        lua_State* lua = luaL_newstate();
        if (!lua) {
            record.message = "failed to allocate Lua state";
            return false;
        }
        *static_cast<int**>(lua_getextraspace(lua)) = &remainingInstructions;
        luaL_openlibs(lua);
        LuaInvocation invoke;
        invoke.scripts = &scripts;
        invoke.record = &record;
        invoke.context = makeEditorToolContext(state, liveHost);
        invoke.context.setLogSink(&record.logs);
        pushEditorTable(lua, invoke);
        lua_sethook(lua, instructionHook, LUA_MASKCOUNT, 100);

        const auto start = std::chrono::steady_clock::now();
        const int load = luaL_loadfile(lua, record.path.string().c_str());
        int call = LUA_OK;
        if (load == LUA_OK) {
            call = lua_pcall(lua, 0, 0, 0);
        }
        const auto end = std::chrono::steady_clock::now();
        record.elapsedMs = std::chrono::duration<float, std::milli>(end - start).count();
        if (load != LUA_OK || call != LUA_OK) {
            record.message = lua_tostring(lua, -1) ? lua_tostring(lua, -1) : "Lua tool script failed";
            record.status = EditorToolScriptStatus::Failed;
        } else {
            record.message = "Script completed.";
            record.status = EditorToolScriptStatus::Success;
        }
        lua_close(lua);

        EditorToolEvent finished;
        finished.type = EditorToolEventType::ScriptFinished;
        finished.scriptPath = record.path;
        finished.success = record.status == EditorToolScriptStatus::Success;
        finished.message = record.message;
        emitEditorToolEvent(state, liveHost, finished);
        return finished.success;
    }

    void showEditorToolScriptsPanel(
        EditorToolScripts& scripts,
        EditorUiState& state,
        EditorLiveApplyHost* liveHost)
    {
#if MANUAL_ENGINE_ENABLE_DEBUG_TOOLS
        if (ImGui::Button("Rescan Tool Scripts")) {
            rescanEditorToolScripts(scripts);
        }
        ImGui::SameLine();
        ImGui::Text("Directory: %s", scripts.scriptDirectory.generic_string().c_str());
        ImGui::Separator();
        if (scripts.scripts.empty()) {
            ImGui::TextUnformatted("No Lua tool scripts discovered.");
        }
        for (size_t index = 0; index < scripts.scripts.size(); ++index) {
            EditorToolScriptRecord& script = scripts.scripts[index];
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::Button("Run")) {
                (void)runEditorToolScript(scripts, index, state, liveHost);
            }
            ImGui::SameLine();
            ImGui::Text("%s", script.displayName.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s %.3f ms", editorToolScriptStatusName(script.status), script.elapsedMs);
            if (!script.message.empty()) {
                ImGui::TextWrapped("%s", script.message.c_str());
            }
            for (const std::string& log : script.logs) {
                ImGui::BulletText("%s", log.c_str());
            }
            ImGui::PopID();
        }
        if (!scripts.diagnostics.empty()) {
            ImGui::SeparatorText("Diagnostics");
            for (const std::string& diagnostic : scripts.diagnostics) {
                ImGui::TextWrapped("%s", diagnostic.c_str());
            }
        }
        if (state.toolHooks) {
            const std::vector<EditorToolEvent> events = state.toolHooks->recentEvents();
            if (!events.empty() && ImGui::CollapsingHeader("Recent Tool Events")) {
                for (auto it = events.rbegin(); it != events.rend(); ++it) {
                    ImGui::Text(
                        "%s: %s%s",
                        editorToolEventName(it->type),
                        it->success ? "ok" : "failed",
                        it->message.empty() ? "" : (" - " + it->message).c_str());
                }
            }
        }
#else
        (void)scripts;
        (void)state;
        (void)liveHost;
#endif
    }

    const char* editorToolEventName(EditorToolEventType type)
    {
        switch (type) {
            case EditorToolEventType::SettingsChanged: return "Settings Changed";
            case EditorToolEventType::ValidationCompleted: return "Validation Completed";
            case EditorToolEventType::RebuildCompleted: return "Rebuild Completed";
            case EditorToolEventType::LightweightApplyCompleted: return "Lightweight Apply Completed";
            case EditorToolEventType::StreamingReloadCompleted: return "Streaming Reload Completed";
            case EditorToolEventType::ScriptStarted: return "Script Started";
            case EditorToolEventType::ScriptFinished: return "Script Finished";
        }
        return "Unknown";
    }

    const char* editorToolScriptStatusName(EditorToolScriptStatus status)
    {
        switch (status) {
            case EditorToolScriptStatus::NotRun: return "Not Run";
            case EditorToolScriptStatus::Success: return "Success";
            case EditorToolScriptStatus::Failed: return "Failed";
        }
        return "Unknown";
    }

}
