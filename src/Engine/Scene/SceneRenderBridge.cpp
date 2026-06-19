#include "Engine/Scene/SceneRenderBridge.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>

namespace Engine {
    namespace {
        constexpr uint32_t InvalidId = UINT32_MAX;

        glm::vec3 translationOf(const glm::mat4& matrix)
        {
            return {matrix[3].x, matrix[3].y, matrix[3].z};
        }
    }

    SceneRenderBridge::SceneRenderBridge(Scene& scene)
        : scene_(scene)
    {
        refreshDiagnostics();
    }

    SceneRenderBridge::~SceneRenderBridge()
    {
        if (lastBackend_) {
            releaseRendererResources(*lastBackend_);
        }
    }

    SceneMeshComponentHandle SceneRenderBridge::attachMesh(const SceneMeshComponentDescriptor& descriptor)
    {
        if (!validActor(descriptor.actor)) {
            return {};
        }

        for (uint32_t index = 0; index < meshes_.size(); ++index) {
            MeshRecord& candidate = meshes_[index];
            if (!candidate.occupied) {
                candidate.occupied = true;
                candidate.generation = nextGeneration(candidate.generation);
                candidate.descriptor = descriptor;
                candidate.instance = {};
                candidate.lastWorld = glm::mat4{1.0f};
                candidate.dirty = true;
                refreshDiagnostics();
                return {index, candidate.generation};
            }
        }

        MeshRecord record;
        record.occupied = true;
        record.generation = nextGeneration(record.generation);
        record.descriptor = descriptor;
        record.lastWorld = glm::mat4{1.0f};
        record.dirty = true;
        meshes_.push_back(std::move(record));
        refreshDiagnostics();
        return {static_cast<uint32_t>(meshes_.size() - 1), meshes_.back().generation};
    }

    bool SceneRenderBridge::detachMesh(SceneMeshComponentHandle component)
    {
        MeshRecord* mesh = record(component);
        if (!mesh) {
            return false;
        }
        freeMeshRecord(*mesh, lastBackend_);
        refreshDiagnostics();
        return true;
    }

    bool SceneRenderBridge::contains(SceneMeshComponentHandle component) const
    {
        return record(component) != nullptr;
    }

    std::optional<SceneMeshComponentDescriptor> SceneRenderBridge::meshDescriptor(SceneMeshComponentHandle component) const
    {
        const MeshRecord* mesh = record(component);
        if (!mesh) {
            return std::nullopt;
        }
        return mesh->descriptor;
    }

    bool SceneRenderBridge::setMeshDescriptor(SceneMeshComponentHandle component, const SceneMeshComponentDescriptor& descriptor)
    {
        MeshRecord* mesh = record(component);
        if (!mesh || !validActor(descriptor.actor)) {
            return false;
        }

        const bool resourceChanged = mesh->descriptor.mesh.id != descriptor.mesh.id;
        if (resourceChanged && lastBackend_) {
            releaseMeshRecord(*mesh, *lastBackend_);
        }
        mesh->descriptor = descriptor;
        mesh->dirty = true;
        refreshDiagnostics();
        return true;
    }

    SceneSkinnedMeshComponentHandle SceneRenderBridge::attachSkinnedMesh(const SceneSkinnedMeshComponentDescriptor& descriptor)
    {
        if (!validActor(descriptor.actor)) {
            return {};
        }

        for (uint32_t index = 0; index < skinnedMeshes_.size(); ++index) {
            SkinnedMeshRecord& candidate = skinnedMeshes_[index];
            if (!candidate.occupied) {
                candidate.occupied = true;
                candidate.generation = nextGeneration(candidate.generation);
                candidate.descriptor = descriptor;
                candidate.instance = {};
                candidate.lastWorld = glm::mat4{1.0f};
                candidate.dirty = true;
                refreshDiagnostics();
                return {index, candidate.generation};
            }
        }

        SkinnedMeshRecord record;
        record.occupied = true;
        record.generation = nextGeneration(record.generation);
        record.descriptor = descriptor;
        record.lastWorld = glm::mat4{1.0f};
        record.dirty = true;
        skinnedMeshes_.push_back(std::move(record));
        refreshDiagnostics();
        return {static_cast<uint32_t>(skinnedMeshes_.size() - 1), skinnedMeshes_.back().generation};
    }

    bool SceneRenderBridge::detachSkinnedMesh(SceneSkinnedMeshComponentHandle component)
    {
        SkinnedMeshRecord* mesh = record(component);
        if (!mesh) {
            return false;
        }
        freeSkinnedMeshRecord(*mesh, lastBackend_);
        refreshDiagnostics();
        return true;
    }

    bool SceneRenderBridge::contains(SceneSkinnedMeshComponentHandle component) const
    {
        return record(component) != nullptr;
    }

    std::optional<SceneSkinnedMeshComponentDescriptor> SceneRenderBridge::skinnedMeshDescriptor(SceneSkinnedMeshComponentHandle component) const
    {
        const SkinnedMeshRecord* mesh = record(component);
        if (!mesh) {
            return std::nullopt;
        }
        return mesh->descriptor;
    }

    bool SceneRenderBridge::setSkinnedMeshDescriptor(
        SceneSkinnedMeshComponentHandle component,
        const SceneSkinnedMeshComponentDescriptor& descriptor)
    {
        SkinnedMeshRecord* mesh = record(component);
        if (!mesh || !validActor(descriptor.actor)) {
            return false;
        }

        const bool resourceChanged = mesh->descriptor.mesh.id != descriptor.mesh.id;
        if (resourceChanged && lastBackend_) {
            releaseSkinnedMeshRecord(*mesh, *lastBackend_);
        }
        mesh->descriptor = descriptor;
        mesh->dirty = true;
        refreshDiagnostics();
        return true;
    }

    SceneLightComponentHandle SceneRenderBridge::attachLight(const SceneLightComponentDescriptor& descriptor)
    {
        if (!validActor(descriptor.actor)) {
            return {};
        }

        for (uint32_t index = 0; index < lights_.size(); ++index) {
            LightRecord& candidate = lights_[index];
            if (!candidate.occupied) {
                candidate.occupied = true;
                candidate.generation = nextGeneration(candidate.generation);
                candidate.descriptor = descriptor;
                candidate.light = {};
                candidate.lastWorld = glm::mat4{1.0f};
                candidate.dirty = true;
                refreshDiagnostics();
                return {index, candidate.generation};
            }
        }

        LightRecord record;
        record.occupied = true;
        record.generation = nextGeneration(record.generation);
        record.descriptor = descriptor;
        record.lastWorld = glm::mat4{1.0f};
        record.dirty = true;
        lights_.push_back(std::move(record));
        refreshDiagnostics();
        return {static_cast<uint32_t>(lights_.size() - 1), lights_.back().generation};
    }

    bool SceneRenderBridge::detachLight(SceneLightComponentHandle component)
    {
        LightRecord* light = record(component);
        if (!light) {
            return false;
        }
        freeLightRecord(*light, lastBackend_);
        refreshDiagnostics();
        return true;
    }

    bool SceneRenderBridge::contains(SceneLightComponentHandle component) const
    {
        return record(component) != nullptr;
    }

    std::optional<SceneLightComponentDescriptor> SceneRenderBridge::lightDescriptor(SceneLightComponentHandle component) const
    {
        const LightRecord* light = record(component);
        if (!light) {
            return std::nullopt;
        }
        return light->descriptor;
    }

    bool SceneRenderBridge::setLightDescriptor(SceneLightComponentHandle component, const SceneLightComponentDescriptor& descriptor)
    {
        LightRecord* light = record(component);
        if (!light || !validActor(descriptor.actor)) {
            return false;
        }
        light->descriptor = descriptor;
        light->dirty = true;
        refreshDiagnostics();
        return true;
    }

    SceneCameraComponentHandle SceneRenderBridge::attachCamera(const SceneCameraComponentDescriptor& descriptor)
    {
        if (!validActor(descriptor.actor)) {
            return {};
        }

        for (uint32_t index = 0; index < cameras_.size(); ++index) {
            CameraRecord& candidate = cameras_[index];
            if (!candidate.occupied) {
                candidate.occupied = true;
                candidate.generation = nextGeneration(candidate.generation);
                candidate.descriptor = descriptor;
                refreshDiagnostics();
                return {index, candidate.generation};
            }
        }

        CameraRecord record;
        record.occupied = true;
        record.generation = nextGeneration(record.generation);
        record.descriptor = descriptor;
        cameras_.push_back(std::move(record));
        refreshDiagnostics();
        return {static_cast<uint32_t>(cameras_.size() - 1), cameras_.back().generation};
    }

    bool SceneRenderBridge::detachCamera(SceneCameraComponentHandle component)
    {
        CameraRecord* camera = record(component);
        if (!camera) {
            return false;
        }
        const uint32_t generation = camera->generation;
        *camera = {};
        camera->generation = generation;
        refreshDiagnostics();
        return true;
    }

    bool SceneRenderBridge::contains(SceneCameraComponentHandle component) const
    {
        return record(component) != nullptr;
    }

    std::optional<SceneCameraComponentDescriptor> SceneRenderBridge::cameraDescriptor(SceneCameraComponentHandle component) const
    {
        const CameraRecord* camera = record(component);
        if (!camera) {
            return std::nullopt;
        }
        return camera->descriptor;
    }

    bool SceneRenderBridge::setCameraDescriptor(SceneCameraComponentHandle component, const SceneCameraComponentDescriptor& descriptor)
    {
        CameraRecord* camera = record(component);
        if (!camera || !validActor(descriptor.actor)) {
            return false;
        }
        camera->descriptor = descriptor;
        refreshDiagnostics();
        return true;
    }

    std::optional<Renderer::RenderView> SceneRenderBridge::buildRenderView(
        SceneCameraComponentHandle camera,
        bgfx::ViewId viewId,
        uint16_t viewportWidth,
        uint16_t viewportHeight)
    {
        CameraRecord* cameraRecord = record(camera);
        if (!cameraRecord || !cameraRecord->descriptor.enabled || viewportWidth == 0 || viewportHeight == 0) {
            return std::nullopt;
        }

        const std::optional<glm::mat4> world = scene_.worldMatrix(cameraRecord->descriptor.actor);
        if (!world.has_value()) {
            return std::nullopt;
        }

        Renderer::RenderView view;
        view.viewId = viewId;
        view.view = glm::inverse(*world);
        view.projection = glm::perspective(
            cameraRecord->descriptor.verticalFieldOfViewRadians,
            static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight),
            cameraRecord->descriptor.nearPlane,
            cameraRecord->descriptor.farPlane);
        view.viewProjection = view.projection * view.view;
        view.cameraPosition = translationOf(*world);
        view.viewportWidth = viewportWidth;
        view.viewportHeight = viewportHeight;
        view.layerMask = cameraRecord->descriptor.layerMask;
        view.enableDistanceCulling = cameraRecord->descriptor.enableDistanceCulling;
        return view;
    }

    void SceneRenderBridge::sync(SceneRenderBackend& backend)
    {
        lastBackend_ = &backend;
        resetLastSyncDiagnostics();

        for (MeshRecord& mesh : meshes_) {
            if (!mesh.occupied) {
                continue;
            }

            const std::optional<glm::mat4> world = scene_.worldMatrix(mesh.descriptor.actor);
            if (!world.has_value()) {
                freeMeshRecord(mesh, &backend);
                ++diagnostics_.invalidOwnerCleanupCount;
                continue;
            }

            if (!mesh.descriptor.enabled) {
                releaseMeshRecord(mesh, backend);
                continue;
            }

            if (!validMesh(mesh.descriptor.mesh)) {
                releaseMeshRecord(mesh, backend);
                ++diagnostics_.missingResourceCount;
                diagnostics_.warnings.push_back("mesh component has invalid static mesh handle");
                continue;
            }

            bool created = false;
            if (!validMeshInstance(mesh.instance)) {
                mesh.instance = backend.createMeshInstance(mesh.descriptor.mesh);
                if (!validMeshInstance(mesh.instance)) {
                    ++diagnostics_.missingResourceCount;
                    diagnostics_.warnings.push_back("backend failed to create mesh instance");
                    continue;
                }
                ++diagnostics_.createCount;
                created = true;
            }

            const bool transformChanged = !matrixEqual(mesh.lastWorld, *world);
            if (created || mesh.dirty || transformChanged) {
                backend.setMeshInstanceTransform(mesh.instance, *world);
                if (mesh.descriptor.material.has_value() && validMaterial(*mesh.descriptor.material)) {
                    backend.setMeshInstanceMaterial(mesh.instance, *mesh.descriptor.material);
                } else {
                    backend.clearMeshInstanceMaterial(mesh.instance);
                }
                backend.setMeshInstanceVisibilityFlags(mesh.instance, mesh.descriptor.visibility);
                backend.setMeshInstanceRenderLayer(mesh.instance, mesh.descriptor.layer);
                backend.setMeshInstanceMaxDrawDistance(mesh.instance, mesh.descriptor.maxDrawDistance);
                if (mesh.descriptor.renderGroup.has_value() && validRenderGroup(*mesh.descriptor.renderGroup)) {
                    backend.setMeshInstanceRenderGroup(mesh.instance, *mesh.descriptor.renderGroup);
                } else {
                    backend.clearMeshInstanceRenderGroup(mesh.instance);
                }
                mesh.lastWorld = *world;
                mesh.dirty = false;
                ++diagnostics_.updateCount;
            }
        }

        for (SkinnedMeshRecord& mesh : skinnedMeshes_) {
            if (!mesh.occupied) {
                continue;
            }

            const std::optional<glm::mat4> world = scene_.worldMatrix(mesh.descriptor.actor);
            if (!world.has_value()) {
                freeSkinnedMeshRecord(mesh, &backend);
                ++diagnostics_.invalidOwnerCleanupCount;
                continue;
            }

            if (!mesh.descriptor.enabled) {
                releaseSkinnedMeshRecord(mesh, backend);
                continue;
            }

            if (!validSkinnedMesh(mesh.descriptor.mesh)) {
                releaseSkinnedMeshRecord(mesh, backend);
                ++diagnostics_.missingResourceCount;
                diagnostics_.warnings.push_back("skinned mesh component has invalid mesh handle");
                continue;
            }

            bool created = false;
            if (!validSkinnedMeshInstance(mesh.instance)) {
                mesh.instance = backend.createSkinnedMeshInstance(mesh.descriptor.mesh);
                if (!validSkinnedMeshInstance(mesh.instance)) {
                    ++diagnostics_.missingResourceCount;
                    diagnostics_.warnings.push_back("backend failed to create skinned mesh instance");
                    continue;
                }
                ++diagnostics_.createCount;
                created = true;
            }

            const bool transformChanged = !matrixEqual(mesh.lastWorld, *world);
            if (created || mesh.dirty || transformChanged) {
                backend.setSkinnedMeshInstanceTransform(mesh.instance, *world);
                backend.setSkinnedMeshInstanceRenderLayer(mesh.instance, mesh.descriptor.layer);
                backend.setSkinnedMeshInstanceMaxDrawDistance(mesh.instance, mesh.descriptor.maxDrawDistance);
                if (mesh.descriptor.renderGroup.has_value() && validRenderGroup(*mesh.descriptor.renderGroup)) {
                    backend.setSkinnedMeshInstanceRenderGroup(mesh.instance, *mesh.descriptor.renderGroup);
                } else {
                    backend.clearSkinnedMeshInstanceRenderGroup(mesh.instance);
                }
                backend.setSkinnedMeshInstanceJointMatrices(mesh.instance, mesh.descriptor.jointMatrices);
                mesh.lastWorld = *world;
                mesh.dirty = false;
                ++diagnostics_.updateCount;
            }
        }

        for (LightRecord& light : lights_) {
            if (!light.occupied) {
                continue;
            }

            const std::optional<glm::mat4> world = scene_.worldMatrix(light.descriptor.actor);
            if (!world.has_value()) {
                freeLightRecord(light, &backend);
                ++diagnostics_.invalidOwnerCleanupCount;
                continue;
            }

            Renderer::LightDescriptor descriptor;
            descriptor.name = light.descriptor.name;
            descriptor.type = light.descriptor.type;
            descriptor.color = light.descriptor.color;
            descriptor.intensity = light.descriptor.intensity;
            descriptor.position = translationOf(*world);
            descriptor.direction = lightDirection(*world);
            descriptor.range = light.descriptor.range;
            descriptor.innerConeAngle = light.descriptor.innerConeAngle;
            descriptor.outerConeAngle = light.descriptor.outerConeAngle;
            descriptor.enabled = light.descriptor.enabled;

            bool created = false;
            if (!validLight(light.light)) {
                light.light = backend.createLight(descriptor);
                if (!validLight(light.light)) {
                    ++diagnostics_.missingResourceCount;
                    diagnostics_.warnings.push_back("backend failed to create light");
                    continue;
                }
                ++diagnostics_.createCount;
                created = true;
            }

            const bool transformChanged = !matrixEqual(light.lastWorld, *world);
            if (!created && (light.dirty || transformChanged)) {
                backend.setLightDescriptor(light.light, descriptor);
            }
            if (created || light.dirty || transformChanged) {
                light.lastWorld = *world;
                light.dirty = false;
                ++diagnostics_.updateCount;
            }
        }

        refreshDiagnostics();
    }

    void SceneRenderBridge::releaseRendererResources(SceneRenderBackend& backend)
    {
        lastBackend_ = &backend;
        resetLastSyncDiagnostics();
        for (MeshRecord& mesh : meshes_) {
            if (mesh.occupied) {
                releaseMeshRecord(mesh, backend);
            }
        }
        for (SkinnedMeshRecord& mesh : skinnedMeshes_) {
            if (mesh.occupied) {
                releaseSkinnedMeshRecord(mesh, backend);
            }
        }
        for (LightRecord& light : lights_) {
            if (light.occupied) {
                releaseLightRecord(light, backend);
            }
        }
        refreshDiagnostics();
    }

    SceneSystemHandle SceneRenderBridge::registerPreRenderSystem(SceneRenderBackend& backend)
    {
        if (preRenderSystem_.has_value() && scene_.contains(*preRenderSystem_)) {
            return *preRenderSystem_;
        }

        SceneSystemDescriptor descriptor;
        descriptor.name = "SceneRenderBridge";
        descriptor.phases = {SceneTickPhase::PreRender};
        descriptor.onTick = [this, &backend](Scene&, const SceneTickContext&) {
            sync(backend);
        };

        const SceneSystemHandle handle = scene_.registerSystem(std::move(descriptor));
        if (isValid(handle)) {
            preRenderSystem_ = handle;
        }
        return handle;
    }

    bool SceneRenderBridge::unregisterPreRenderSystem()
    {
        if (!preRenderSystem_.has_value()) {
            return false;
        }
        const bool unregistered = scene_.unregisterSystem(*preRenderSystem_);
        if (unregistered) {
            preRenderSystem_ = std::nullopt;
        }
        return unregistered;
    }

    SceneRenderBridgeDiagnostics SceneRenderBridge::diagnostics() const
    {
        return diagnostics_;
    }

    SceneRenderBridge::MeshRecord* SceneRenderBridge::record(SceneMeshComponentHandle component)
    {
        if (!isValid(component) || component.index >= meshes_.size()) {
            return nullptr;
        }
        MeshRecord& mesh = meshes_[component.index];
        if (!mesh.occupied || mesh.generation != component.generation) {
            return nullptr;
        }
        return &mesh;
    }

    const SceneRenderBridge::MeshRecord* SceneRenderBridge::record(SceneMeshComponentHandle component) const
    {
        if (!isValid(component) || component.index >= meshes_.size()) {
            return nullptr;
        }
        const MeshRecord& mesh = meshes_[component.index];
        if (!mesh.occupied || mesh.generation != component.generation) {
            return nullptr;
        }
        return &mesh;
    }

    SceneRenderBridge::SkinnedMeshRecord* SceneRenderBridge::record(SceneSkinnedMeshComponentHandle component)
    {
        if (!isValid(component) || component.index >= skinnedMeshes_.size()) {
            return nullptr;
        }
        SkinnedMeshRecord& mesh = skinnedMeshes_[component.index];
        if (!mesh.occupied || mesh.generation != component.generation) {
            return nullptr;
        }
        return &mesh;
    }

    const SceneRenderBridge::SkinnedMeshRecord* SceneRenderBridge::record(SceneSkinnedMeshComponentHandle component) const
    {
        if (!isValid(component) || component.index >= skinnedMeshes_.size()) {
            return nullptr;
        }
        const SkinnedMeshRecord& mesh = skinnedMeshes_[component.index];
        if (!mesh.occupied || mesh.generation != component.generation) {
            return nullptr;
        }
        return &mesh;
    }

    SceneRenderBridge::LightRecord* SceneRenderBridge::record(SceneLightComponentHandle component)
    {
        if (!isValid(component) || component.index >= lights_.size()) {
            return nullptr;
        }
        LightRecord& light = lights_[component.index];
        if (!light.occupied || light.generation != component.generation) {
            return nullptr;
        }
        return &light;
    }

    const SceneRenderBridge::LightRecord* SceneRenderBridge::record(SceneLightComponentHandle component) const
    {
        if (!isValid(component) || component.index >= lights_.size()) {
            return nullptr;
        }
        const LightRecord& light = lights_[component.index];
        if (!light.occupied || light.generation != component.generation) {
            return nullptr;
        }
        return &light;
    }

    SceneRenderBridge::CameraRecord* SceneRenderBridge::record(SceneCameraComponentHandle component)
    {
        if (!isValid(component) || component.index >= cameras_.size()) {
            return nullptr;
        }
        CameraRecord& camera = cameras_[component.index];
        if (!camera.occupied || camera.generation != component.generation) {
            return nullptr;
        }
        return &camera;
    }

    const SceneRenderBridge::CameraRecord* SceneRenderBridge::record(SceneCameraComponentHandle component) const
    {
        if (!isValid(component) || component.index >= cameras_.size()) {
            return nullptr;
        }
        const CameraRecord& camera = cameras_[component.index];
        if (!camera.occupied || camera.generation != component.generation) {
            return nullptr;
        }
        return &camera;
    }

    uint32_t SceneRenderBridge::nextGeneration(uint32_t generation) const
    {
        if (generation == std::numeric_limits<uint32_t>::max()) {
            return 1;
        }
        return generation + 1;
    }

    bool SceneRenderBridge::validActor(SceneActorHandle actor) const
    {
        return scene_.contains(actor);
    }

    bool SceneRenderBridge::validMesh(Renderer::StaticMeshHandle mesh) const
    {
        return mesh.id != InvalidId;
    }

    bool SceneRenderBridge::validSkinnedMesh(Renderer::SkinnedMeshHandle mesh) const
    {
        return mesh.id != InvalidId;
    }

    bool SceneRenderBridge::validMaterial(Renderer::MaterialHandle material) const
    {
        return material.id != InvalidId;
    }

    bool SceneRenderBridge::validRenderGroup(Renderer::RenderGroupHandle group) const
    {
        return group.id != InvalidId;
    }

    bool SceneRenderBridge::validMeshInstance(Renderer::MeshInstanceHandle instance) const
    {
        return instance.id != InvalidId;
    }

    bool SceneRenderBridge::validSkinnedMeshInstance(Renderer::SkinnedMeshInstanceHandle instance) const
    {
        return instance.id != InvalidId;
    }

    bool SceneRenderBridge::validLight(Renderer::LightHandle light) const
    {
        return light.id != InvalidId;
    }

    bool SceneRenderBridge::matrixEqual(const glm::mat4& lhs, const glm::mat4& rhs) const
    {
        constexpr float Epsilon = 0.00001f;
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                if (std::abs(lhs[column][row] - rhs[column][row]) > Epsilon) {
                    return false;
                }
            }
        }
        return true;
    }

    glm::vec3 SceneRenderBridge::lightDirection(const glm::mat4& world) const
    {
        const glm::vec3 direction = glm::normalize(glm::vec3{world * glm::vec4{0.0f, 0.0f, -1.0f, 0.0f}});
        if (!std::isfinite(direction.x) || !std::isfinite(direction.y) || !std::isfinite(direction.z)) {
            return {0.0f, -1.0f, 0.0f};
        }
        return direction;
    }

    void SceneRenderBridge::releaseMeshRecord(MeshRecord& record, SceneRenderBackend& backend)
    {
        if (validMeshInstance(record.instance)) {
            backend.destroyMeshInstance(record.instance);
            record.instance = {};
            ++diagnostics_.destroyCount;
        }
    }

    void SceneRenderBridge::releaseSkinnedMeshRecord(SkinnedMeshRecord& record, SceneRenderBackend& backend)
    {
        if (validSkinnedMeshInstance(record.instance)) {
            backend.destroySkinnedMeshInstance(record.instance);
            record.instance = {};
            ++diagnostics_.destroyCount;
        }
    }

    void SceneRenderBridge::releaseLightRecord(LightRecord& record, SceneRenderBackend& backend)
    {
        if (validLight(record.light)) {
            backend.destroyLight(record.light);
            record.light = {};
            ++diagnostics_.destroyCount;
        }
    }

    void SceneRenderBridge::freeMeshRecord(MeshRecord& record, SceneRenderBackend* backend)
    {
        if (backend) {
            releaseMeshRecord(record, *backend);
        }
        const uint32_t generation = record.generation;
        record = {};
        record.generation = generation;
    }

    void SceneRenderBridge::freeSkinnedMeshRecord(SkinnedMeshRecord& record, SceneRenderBackend* backend)
    {
        if (backend) {
            releaseSkinnedMeshRecord(record, *backend);
        }
        const uint32_t generation = record.generation;
        record = {};
        record.generation = generation;
    }

    void SceneRenderBridge::freeLightRecord(LightRecord& record, SceneRenderBackend* backend)
    {
        if (backend) {
            releaseLightRecord(record, *backend);
        }
        const uint32_t generation = record.generation;
        record = {};
        record.generation = generation;
    }

    void SceneRenderBridge::resetLastSyncDiagnostics()
    {
        const uint32_t meshCount = diagnostics_.meshComponentCount;
        const uint32_t skinnedCount = diagnostics_.skinnedMeshComponentCount;
        const uint32_t lightCount = diagnostics_.lightComponentCount;
        const uint32_t cameraCount = diagnostics_.cameraComponentCount;
        diagnostics_ = {};
        diagnostics_.meshComponentCount = meshCount;
        diagnostics_.skinnedMeshComponentCount = skinnedCount;
        diagnostics_.lightComponentCount = lightCount;
        diagnostics_.cameraComponentCount = cameraCount;
    }

    void SceneRenderBridge::refreshDiagnostics()
    {
        const uint32_t invalidOwnerCleanupCount = diagnostics_.invalidOwnerCleanupCount;
        const uint32_t createCount = diagnostics_.createCount;
        const uint32_t destroyCount = diagnostics_.destroyCount;
        const uint32_t updateCount = diagnostics_.updateCount;
        const uint32_t missingResourceCount = diagnostics_.missingResourceCount;
        std::vector<std::string> warnings = diagnostics_.warnings;

        diagnostics_ = {};
        diagnostics_.invalidOwnerCleanupCount = invalidOwnerCleanupCount;
        diagnostics_.createCount = createCount;
        diagnostics_.destroyCount = destroyCount;
        diagnostics_.updateCount = updateCount;
        diagnostics_.missingResourceCount = missingResourceCount;
        diagnostics_.warnings = std::move(warnings);

        for (const MeshRecord& mesh : meshes_) {
            if (!mesh.occupied) {
                continue;
            }
            ++diagnostics_.meshComponentCount;
            if (validMeshInstance(mesh.instance)) {
                ++diagnostics_.liveMeshInstanceCount;
            }
        }
        for (const SkinnedMeshRecord& mesh : skinnedMeshes_) {
            if (!mesh.occupied) {
                continue;
            }
            ++diagnostics_.skinnedMeshComponentCount;
            if (validSkinnedMeshInstance(mesh.instance)) {
                ++diagnostics_.liveSkinnedInstanceCount;
            }
        }
        for (const LightRecord& light : lights_) {
            if (!light.occupied) {
                continue;
            }
            ++diagnostics_.lightComponentCount;
            if (validLight(light.light)) {
                ++diagnostics_.liveLightCount;
            }
        }
        for (const CameraRecord& camera : cameras_) {
            if (camera.occupied) {
                ++diagnostics_.cameraComponentCount;
            }
        }
    }
}
