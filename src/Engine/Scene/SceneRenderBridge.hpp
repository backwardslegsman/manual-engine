#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/Scene/Scene.hpp"
#include "Renderer/Scene.hpp"

namespace Engine {
    struct SceneMeshComponentHandle {
        uint32_t index = UINT32_MAX;
        uint32_t generation = 0;
    };

    struct SceneSkinnedMeshComponentHandle {
        uint32_t index = UINT32_MAX;
        uint32_t generation = 0;
    };

    struct SceneLightComponentHandle {
        uint32_t index = UINT32_MAX;
        uint32_t generation = 0;
    };

    struct SceneCameraComponentHandle {
        uint32_t index = UINT32_MAX;
        uint32_t generation = 0;
    };

    [[nodiscard]] constexpr bool isValid(SceneMeshComponentHandle handle)
    {
        return handle.index != UINT32_MAX && handle.generation != 0;
    }

    [[nodiscard]] constexpr bool isValid(SceneSkinnedMeshComponentHandle handle)
    {
        return handle.index != UINT32_MAX && handle.generation != 0;
    }

    [[nodiscard]] constexpr bool isValid(SceneLightComponentHandle handle)
    {
        return handle.index != UINT32_MAX && handle.generation != 0;
    }

    [[nodiscard]] constexpr bool isValid(SceneCameraComponentHandle handle)
    {
        return handle.index != UINT32_MAX && handle.generation != 0;
    }

    [[nodiscard]] constexpr bool operator==(SceneMeshComponentHandle lhs, SceneMeshComponentHandle rhs)
    {
        return lhs.index == rhs.index && lhs.generation == rhs.generation;
    }

    [[nodiscard]] constexpr bool operator!=(SceneMeshComponentHandle lhs, SceneMeshComponentHandle rhs)
    {
        return !(lhs == rhs);
    }

    [[nodiscard]] constexpr bool operator==(SceneSkinnedMeshComponentHandle lhs, SceneSkinnedMeshComponentHandle rhs)
    {
        return lhs.index == rhs.index && lhs.generation == rhs.generation;
    }

    [[nodiscard]] constexpr bool operator!=(SceneSkinnedMeshComponentHandle lhs, SceneSkinnedMeshComponentHandle rhs)
    {
        return !(lhs == rhs);
    }

    [[nodiscard]] constexpr bool operator==(SceneLightComponentHandle lhs, SceneLightComponentHandle rhs)
    {
        return lhs.index == rhs.index && lhs.generation == rhs.generation;
    }

    [[nodiscard]] constexpr bool operator!=(SceneLightComponentHandle lhs, SceneLightComponentHandle rhs)
    {
        return !(lhs == rhs);
    }

    [[nodiscard]] constexpr bool operator==(SceneCameraComponentHandle lhs, SceneCameraComponentHandle rhs)
    {
        return lhs.index == rhs.index && lhs.generation == rhs.generation;
    }

    [[nodiscard]] constexpr bool operator!=(SceneCameraComponentHandle lhs, SceneCameraComponentHandle rhs)
    {
        return !(lhs == rhs);
    }

    struct SceneMeshComponentDescriptor {
        SceneActorHandle actor;
        Renderer::StaticMeshHandle mesh;
        std::optional<Renderer::MaterialHandle> material;
        Renderer::RenderLayer layer = Renderer::RenderLayer::Props;
        Renderer::VisibilityFlags visibility = Renderer::VisibilityFlags::Visible;
        float maxDrawDistance = 0.0f;
        std::optional<Renderer::RenderGroupHandle> renderGroup;
        bool enabled = true;
    };

    struct SceneSkinnedMeshComponentDescriptor {
        SceneActorHandle actor;
        Renderer::SkinnedMeshHandle mesh;
        Renderer::RenderLayer layer = Renderer::RenderLayer::Props;
        float maxDrawDistance = 0.0f;
        std::optional<Renderer::RenderGroupHandle> renderGroup;
        std::vector<glm::mat4> jointMatrices;
        bool enabled = true;
    };

    struct SceneLightComponentDescriptor {
        SceneActorHandle actor;
        Renderer::LightType type = Renderer::LightType::Point;
        std::string name;
        glm::vec3 color{1.0f};
        float intensity = 1.0f;
        float range = 0.0f;
        float innerConeAngle = 0.0f;
        float outerConeAngle = 0.7853982f;
        bool enabled = true;
    };

    struct SceneCameraComponentDescriptor {
        SceneActorHandle actor;
        float verticalFieldOfViewRadians = 1.0471976f;
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
        uint32_t layerMask = static_cast<uint32_t>(Renderer::RenderLayer::All);
        bool enableDistanceCulling = true;
        bool enabled = true;
        bool primary = false;
    };

    struct SceneRenderBridgeDiagnostics {
        uint32_t meshComponentCount = 0;
        uint32_t liveMeshInstanceCount = 0;
        uint32_t skinnedMeshComponentCount = 0;
        uint32_t liveSkinnedInstanceCount = 0;
        uint32_t lightComponentCount = 0;
        uint32_t liveLightCount = 0;
        uint32_t cameraComponentCount = 0;
        uint32_t invalidOwnerCleanupCount = 0;
        uint32_t createCount = 0;
        uint32_t destroyCount = 0;
        uint32_t updateCount = 0;
        uint32_t missingResourceCount = 0;
        std::vector<std::string> warnings;
    };

    class SceneRenderBackend {
    public:
        virtual ~SceneRenderBackend() = default;

        [[nodiscard]] virtual Renderer::MeshInstanceHandle createMeshInstance(Renderer::StaticMeshHandle mesh) = 0;
        virtual void destroyMeshInstance(Renderer::MeshInstanceHandle instance) = 0;
        virtual void setMeshInstanceTransform(Renderer::MeshInstanceHandle instance, const glm::mat4& transform) = 0;
        virtual void setMeshInstanceMaterial(Renderer::MeshInstanceHandle instance, Renderer::MaterialHandle material) = 0;
        virtual void clearMeshInstanceMaterial(Renderer::MeshInstanceHandle instance) = 0;
        virtual void setMeshInstanceRenderLayer(Renderer::MeshInstanceHandle instance, Renderer::RenderLayer layer) = 0;
        virtual void setMeshInstanceVisibilityFlags(Renderer::MeshInstanceHandle instance, Renderer::VisibilityFlags flags) = 0;
        virtual void setMeshInstanceMaxDrawDistance(Renderer::MeshInstanceHandle instance, float maxDrawDistance) = 0;
        virtual void setMeshInstanceRenderGroup(Renderer::MeshInstanceHandle instance, Renderer::RenderGroupHandle group) = 0;
        virtual void clearMeshInstanceRenderGroup(Renderer::MeshInstanceHandle instance) = 0;

        [[nodiscard]] virtual Renderer::SkinnedMeshInstanceHandle createSkinnedMeshInstance(Renderer::SkinnedMeshHandle mesh) = 0;
        virtual void destroySkinnedMeshInstance(Renderer::SkinnedMeshInstanceHandle instance) = 0;
        virtual void setSkinnedMeshInstanceTransform(Renderer::SkinnedMeshInstanceHandle instance, const glm::mat4& transform) = 0;
        virtual void setSkinnedMeshInstanceRenderLayer(Renderer::SkinnedMeshInstanceHandle instance, Renderer::RenderLayer layer) = 0;
        virtual void setSkinnedMeshInstanceMaxDrawDistance(Renderer::SkinnedMeshInstanceHandle instance, float maxDrawDistance) = 0;
        virtual void setSkinnedMeshInstanceRenderGroup(Renderer::SkinnedMeshInstanceHandle instance, Renderer::RenderGroupHandle group) = 0;
        virtual void clearSkinnedMeshInstanceRenderGroup(Renderer::SkinnedMeshInstanceHandle instance) = 0;
        virtual void setSkinnedMeshInstanceJointMatrices(
            Renderer::SkinnedMeshInstanceHandle instance,
            const std::vector<glm::mat4>& matrices) = 0;

        [[nodiscard]] virtual Renderer::LightHandle createLight(const Renderer::LightDescriptor& descriptor) = 0;
        virtual void destroyLight(Renderer::LightHandle light) = 0;
        virtual void setLightDescriptor(Renderer::LightHandle light, const Renderer::LightDescriptor& descriptor) = 0;
    };

    class SceneRenderBridge {
    public:
        explicit SceneRenderBridge(Scene& scene);
        ~SceneRenderBridge();

        SceneRenderBridge(const SceneRenderBridge&) = delete;
        SceneRenderBridge& operator=(const SceneRenderBridge&) = delete;

        [[nodiscard]] SceneMeshComponentHandle attachMesh(const SceneMeshComponentDescriptor& descriptor);
        bool detachMesh(SceneMeshComponentHandle component);
        [[nodiscard]] bool contains(SceneMeshComponentHandle component) const;
        [[nodiscard]] std::optional<SceneMeshComponentDescriptor> meshDescriptor(SceneMeshComponentHandle component) const;
        bool setMeshDescriptor(SceneMeshComponentHandle component, const SceneMeshComponentDescriptor& descriptor);

        [[nodiscard]] SceneSkinnedMeshComponentHandle attachSkinnedMesh(const SceneSkinnedMeshComponentDescriptor& descriptor);
        bool detachSkinnedMesh(SceneSkinnedMeshComponentHandle component);
        [[nodiscard]] bool contains(SceneSkinnedMeshComponentHandle component) const;
        [[nodiscard]] std::optional<SceneSkinnedMeshComponentDescriptor> skinnedMeshDescriptor(SceneSkinnedMeshComponentHandle component) const;
        bool setSkinnedMeshDescriptor(SceneSkinnedMeshComponentHandle component, const SceneSkinnedMeshComponentDescriptor& descriptor);

        [[nodiscard]] SceneLightComponentHandle attachLight(const SceneLightComponentDescriptor& descriptor);
        bool detachLight(SceneLightComponentHandle component);
        [[nodiscard]] bool contains(SceneLightComponentHandle component) const;
        [[nodiscard]] std::optional<SceneLightComponentDescriptor> lightDescriptor(SceneLightComponentHandle component) const;
        bool setLightDescriptor(SceneLightComponentHandle component, const SceneLightComponentDescriptor& descriptor);

        [[nodiscard]] SceneCameraComponentHandle attachCamera(const SceneCameraComponentDescriptor& descriptor);
        bool detachCamera(SceneCameraComponentHandle component);
        [[nodiscard]] bool contains(SceneCameraComponentHandle component) const;
        [[nodiscard]] std::optional<SceneCameraComponentDescriptor> cameraDescriptor(SceneCameraComponentHandle component) const;
        bool setCameraDescriptor(SceneCameraComponentHandle component, const SceneCameraComponentDescriptor& descriptor);
        [[nodiscard]] std::optional<Renderer::RenderView> buildRenderView(
            SceneCameraComponentHandle camera,
            bgfx::ViewId viewId,
            uint16_t viewportWidth,
            uint16_t viewportHeight);

        void sync(SceneRenderBackend& backend);
        void releaseRendererResources(SceneRenderBackend& backend);
        [[nodiscard]] SceneSystemHandle registerPreRenderSystem(SceneRenderBackend& backend);
        bool unregisterPreRenderSystem();
        [[nodiscard]] SceneRenderBridgeDiagnostics diagnostics() const;

    private:
        struct MeshRecord {
            uint32_t generation = 0;
            bool occupied = false;
            SceneMeshComponentDescriptor descriptor;
            Renderer::MeshInstanceHandle instance;
            glm::mat4 lastWorld{1.0f};
            bool dirty = true;
        };

        struct SkinnedMeshRecord {
            uint32_t generation = 0;
            bool occupied = false;
            SceneSkinnedMeshComponentDescriptor descriptor;
            Renderer::SkinnedMeshInstanceHandle instance;
            glm::mat4 lastWorld{1.0f};
            bool dirty = true;
        };

        struct LightRecord {
            uint32_t generation = 0;
            bool occupied = false;
            SceneLightComponentDescriptor descriptor;
            Renderer::LightHandle light;
            glm::mat4 lastWorld{1.0f};
            bool dirty = true;
        };

        struct CameraRecord {
            uint32_t generation = 0;
            bool occupied = false;
            SceneCameraComponentDescriptor descriptor;
        };

        MeshRecord* record(SceneMeshComponentHandle component);
        const MeshRecord* record(SceneMeshComponentHandle component) const;
        SkinnedMeshRecord* record(SceneSkinnedMeshComponentHandle component);
        const SkinnedMeshRecord* record(SceneSkinnedMeshComponentHandle component) const;
        LightRecord* record(SceneLightComponentHandle component);
        const LightRecord* record(SceneLightComponentHandle component) const;
        CameraRecord* record(SceneCameraComponentHandle component);
        const CameraRecord* record(SceneCameraComponentHandle component) const;

        [[nodiscard]] uint32_t nextGeneration(uint32_t generation) const;
        [[nodiscard]] bool validActor(SceneActorHandle actor) const;
        [[nodiscard]] bool validMesh(Renderer::StaticMeshHandle mesh) const;
        [[nodiscard]] bool validSkinnedMesh(Renderer::SkinnedMeshHandle mesh) const;
        [[nodiscard]] bool validMaterial(Renderer::MaterialHandle material) const;
        [[nodiscard]] bool validRenderGroup(Renderer::RenderGroupHandle group) const;
        [[nodiscard]] bool validMeshInstance(Renderer::MeshInstanceHandle instance) const;
        [[nodiscard]] bool validSkinnedMeshInstance(Renderer::SkinnedMeshInstanceHandle instance) const;
        [[nodiscard]] bool validLight(Renderer::LightHandle light) const;
        [[nodiscard]] bool matrixEqual(const glm::mat4& lhs, const glm::mat4& rhs) const;
        [[nodiscard]] glm::vec3 lightDirection(const glm::mat4& world) const;

        void releaseMeshRecord(MeshRecord& record, SceneRenderBackend& backend);
        void releaseSkinnedMeshRecord(SkinnedMeshRecord& record, SceneRenderBackend& backend);
        void releaseLightRecord(LightRecord& record, SceneRenderBackend& backend);
        void freeMeshRecord(MeshRecord& record, SceneRenderBackend* backend);
        void freeSkinnedMeshRecord(SkinnedMeshRecord& record, SceneRenderBackend* backend);
        void freeLightRecord(LightRecord& record, SceneRenderBackend* backend);
        void resetLastSyncDiagnostics();
        void refreshDiagnostics();

        Scene& scene_;
        SceneRenderBackend* lastBackend_ = nullptr;
        std::optional<SceneSystemHandle> preRenderSystem_;
        std::vector<MeshRecord> meshes_;
        std::vector<SkinnedMeshRecord> skinnedMeshes_;
        std::vector<LightRecord> lights_;
        std::vector<CameraRecord> cameras_;
        SceneRenderBridgeDiagnostics diagnostics_;
    };
}
