#include "Renderer/Scene.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <glm/gtc/matrix_transform.hpp>

#include "Assets/Assimp/Importer.hpp"
#include "Renderer/VertexLayouts.hpp"
#include "Renderer/core.hpp"

namespace {
    struct MaterialResource {
        glm::vec4 baseColorFactor{1.0f};
        float metallicFactor = 0.0f;
        float roughnessFactor = 1.0f;
        Renderer::TextureHandle baseColorTexture;
        Renderer::TextureHandle normalTexture;
        Renderer::TextureHandle metallicTexture;
        Renderer::TextureHandle roughnessTexture;
    };

    struct SubmeshResource {
        bgfx::VertexBufferHandle vertexBuffer = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle indexBuffer = BGFX_INVALID_HANDLE;
        uint32_t indexCount = 0;
        Renderer::MaterialHandle material;
    };

    struct StaticMeshResource {
        bool alive = false;
        Renderer::Aabb localBounds;
        bool hasBounds = false;
        std::vector<SubmeshResource> submeshes;
    };

    struct MeshInstanceResource {
        bool alive = false;
        Renderer::StaticMeshHandle mesh;
        glm::vec3 position{};
        glm::vec3 rotation{};
        glm::vec3 scale{1.0f};
        glm::mat4 explicitTransform{1.0f};
        bool usesExplicitTransform = false;
        Renderer::TextureHandle baseColorOverride;
    };

    struct TerrainTileResource {
        bool alive = false;
        bgfx::VertexBufferHandle vertexBuffer = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle indexBuffer = BGFX_INVALID_HANDLE;
        uint32_t indexCount = 0;
        Renderer::TextureHandle baseColorTexture;
        Renderer::Aabb worldBounds;
        bool hasBounds = false;
    };

    std::vector<MaterialResource> g_materials;
    std::vector<StaticMeshResource> g_meshes;
    std::vector<MeshInstanceResource> g_instances;
    std::vector<TerrainTileResource> g_terrainTiles;

    bgfx::ProgramHandle g_meshProgram = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_baseColorSampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_normalSampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_metallicSampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_roughnessSampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_baseColorFactor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_materialParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_textureFlags = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_lightDirection = BGFX_INVALID_HANDLE;

    Renderer::TextureHandle g_whiteTexture;
    Renderer::TextureHandle g_flatNormalTexture;
    Renderer::TextureHandle g_blackTexture;

    bool isValidStaticMesh(Renderer::StaticMeshHandle handle)
    {
        return handle.id < g_meshes.size() && g_meshes[handle.id].alive;
    }

    bool isValidMaterial(Renderer::MaterialHandle handle)
    {
        return handle.id < g_materials.size();
    }

    bool isValidMeshInstance(Renderer::MeshInstanceHandle handle)
    {
        return handle.id < g_instances.size() && g_instances[handle.id].alive;
    }

    bool isValidTerrain(Renderer::TerrainHandle handle)
    {
        return handle.id < g_terrainTiles.size() && g_terrainTiles[handle.id].alive;
    }

    bool isFiniteVec3(const glm::vec3& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    bool isValidAabb(const Renderer::Aabb& bounds)
    {
        return isFiniteVec3(bounds.min) &&
            isFiniteVec3(bounds.max) &&
            bounds.min.x <= bounds.max.x &&
            bounds.min.y <= bounds.max.y &&
            bounds.min.z <= bounds.max.z;
    }

    Renderer::Aabb emptyAabb()
    {
        const float maxFloat = std::numeric_limits<float>::max();
        return {{maxFloat, maxFloat, maxFloat}, {-maxFloat, -maxFloat, -maxFloat}};
    }

    void includePoint(Renderer::Aabb& bounds, const glm::vec3& point)
    {
        bounds.min = glm::min(bounds.min, point);
        bounds.max = glm::max(bounds.max, point);
    }

    void includeAabb(Renderer::Aabb& bounds, const Renderer::Aabb& other)
    {
        includePoint(bounds, other.min);
        includePoint(bounds, other.max);
    }

    Renderer::Aabb transformAabb(const Renderer::Aabb& bounds, const glm::mat4& transform)
    {
        Renderer::Aabb transformed = emptyAabb();
        for (uint32_t corner = 0; corner < 8; ++corner) {
            const glm::vec3 point{
                (corner & 1) ? bounds.max.x : bounds.min.x,
                (corner & 2) ? bounds.max.y : bounds.min.y,
                (corner & 4) ? bounds.max.z : bounds.min.z,
            };
            const glm::vec4 transformedPoint = transform * glm::vec4{point, 1.0f};
            includePoint(transformed, glm::vec3{transformedPoint});
        }
        return transformed;
    }

    glm::vec4 matrixRow(const glm::mat4& matrix, uint32_t row)
    {
        return {matrix[0][row], matrix[1][row], matrix[2][row], matrix[3][row]};
    }

    glm::vec4 normalizePlane(const glm::vec4& plane)
    {
        const float length = glm::length(glm::vec3{plane});
        if (length <= 0.0f || !std::isfinite(length)) {
            return plane;
        }
        return plane / length;
    }

    Renderer::Frustum makeFrustum(const glm::mat4& viewProjection)
    {
        const glm::vec4 row0 = matrixRow(viewProjection, 0);
        const glm::vec4 row1 = matrixRow(viewProjection, 1);
        const glm::vec4 row2 = matrixRow(viewProjection, 2);
        const glm::vec4 row3 = matrixRow(viewProjection, 3);

        Renderer::Frustum frustum;
        frustum.planes[0] = normalizePlane(row3 + row0);
        frustum.planes[1] = normalizePlane(row3 - row0);
        frustum.planes[2] = normalizePlane(row3 + row1);
        frustum.planes[3] = normalizePlane(row3 - row1);
        frustum.planes[4] = normalizePlane(row3 + row2);
        frustum.planes[5] = normalizePlane(row3 - row2);
        return frustum;
    }

    bool intersects(const Renderer::Frustum& frustum, const Renderer::Aabb& bounds)
    {
        if (!isValidAabb(bounds)) {
            return true;
        }

        for (const glm::vec4& plane : frustum.planes) {
            const glm::vec3 positive{
                plane.x >= 0.0f ? bounds.max.x : bounds.min.x,
                plane.y >= 0.0f ? bounds.max.y : bounds.min.y,
                plane.z >= 0.0f ? bounds.max.z : bounds.min.z,
            };
            if (glm::dot(glm::vec3{plane}, positive) + plane.w < 0.0f) {
                return false;
            }
        }

        return true;
    }

    glm::mat4 composeTransform(const MeshInstanceResource& instance)
    {
        if (instance.usesExplicitTransform) {
            return instance.explicitTransform;
        }

        glm::mat4 transform{1.0f};
        transform = glm::translate(transform, instance.position);
        transform = glm::rotate(transform, instance.rotation.x, {1.0f, 0.0f, 0.0f});
        transform = glm::rotate(transform, instance.rotation.y, {0.0f, 1.0f, 0.0f});
        transform = glm::rotate(transform, instance.rotation.z, {0.0f, 0.0f, 1.0f});
        transform = glm::scale(transform, instance.scale);
        return transform;
    }

    Renderer::MaterialHandle addMaterial(MaterialResource material)
    {
        const uint32_t id = static_cast<uint32_t>(g_materials.size());
        g_materials.push_back(material);
        return {id};
    }

    Renderer::StaticMeshHandle addMesh(StaticMeshResource mesh)
    {
        mesh.alive = true;
        for (uint32_t index = 0; index < g_meshes.size(); ++index) {
            if (!g_meshes[index].alive) {
                g_meshes[index] = std::move(mesh);
                return {index};
            }
        }

        const uint32_t id = static_cast<uint32_t>(g_meshes.size());
        g_meshes.push_back(std::move(mesh));
        return {id};
    }

    Renderer::TextureHandle loadMaterialTexture(
        const std::filesystem::path& meshDirectory,
        const std::filesystem::path& texturePath,
        Renderer::TextureHandle fallback)
    {
        if (texturePath.empty()) {
            return fallback;
        }

        const std::filesystem::path resolvedTexturePath =
            texturePath.is_absolute() ? texturePath : meshDirectory / texturePath;
        const Renderer::TextureHandle texture = Renderer::loadTexture(resolvedTexturePath);
        return Renderer::isValid(texture) ? texture : fallback;
    }
}

namespace Renderer {
    bool initSceneRenderer()
    {
        g_whiteTexture = createSolidTexture(255, 255, 255, 255);
        g_flatNormalTexture = createSolidTexture(128, 128, 255, 255);
        g_blackTexture = createSolidTexture(0, 0, 0, 255);

        bgfx::ShaderHandle vsh = loadShader("vs_mesh.bin");
        bgfx::ShaderHandle fsh = loadShader("fs_mesh.bin");
        if (!bgfx::isValid(vsh) || !bgfx::isValid(fsh)) {
            return false;
        }

        g_meshProgram = bgfx::createProgram(vsh, fsh, true);
        if (!bgfx::isValid(g_meshProgram)) {
            return false;
        }

        g_baseColorSampler = bgfx::createUniform("s_baseColor", bgfx::UniformType::Sampler);
        g_normalSampler = bgfx::createUniform("s_normalMap", bgfx::UniformType::Sampler);
        g_metallicSampler = bgfx::createUniform("s_metallicMap", bgfx::UniformType::Sampler);
        g_roughnessSampler = bgfx::createUniform("s_roughnessMap", bgfx::UniformType::Sampler);
        g_baseColorFactor = bgfx::createUniform("u_baseColorFactor", bgfx::UniformType::Vec4);
        g_materialParams = bgfx::createUniform("u_materialParams", bgfx::UniformType::Vec4);
        g_textureFlags = bgfx::createUniform("u_textureFlags", bgfx::UniformType::Vec4);
        g_lightDirection = bgfx::createUniform("u_lightDirection", bgfx::UniformType::Vec4);

        MaterialResource defaultMaterial;
        defaultMaterial.baseColorTexture = g_whiteTexture;
        defaultMaterial.normalTexture = g_flatNormalTexture;
        defaultMaterial.metallicTexture = g_blackTexture;
        defaultMaterial.roughnessTexture = g_whiteTexture;
        addMaterial(defaultMaterial);

        return true;
    }

    void shutdownSceneRenderer()
    {
        for (StaticMeshResource& mesh : g_meshes) {
            if (!mesh.alive) {
                continue;
            }

            for (SubmeshResource& submesh : mesh.submeshes) {
                if (bgfx::isValid(submesh.vertexBuffer)) {
                    bgfx::destroy(submesh.vertexBuffer);
                    submesh.vertexBuffer = BGFX_INVALID_HANDLE;
                }
                if (bgfx::isValid(submesh.indexBuffer)) {
                    bgfx::destroy(submesh.indexBuffer);
                    submesh.indexBuffer = BGFX_INVALID_HANDLE;
                }
            }
        }
        g_meshes.clear();
        g_materials.clear();
        g_instances.clear();

        for (TerrainTileResource& terrain : g_terrainTiles) {
            if (bgfx::isValid(terrain.vertexBuffer)) {
                bgfx::destroy(terrain.vertexBuffer);
                terrain.vertexBuffer = BGFX_INVALID_HANDLE;
            }
            if (bgfx::isValid(terrain.indexBuffer)) {
                bgfx::destroy(terrain.indexBuffer);
                terrain.indexBuffer = BGFX_INVALID_HANDLE;
            }
        }
        g_terrainTiles.clear();

        const std::array uniforms = {
            g_baseColorSampler,
            g_normalSampler,
            g_metallicSampler,
            g_roughnessSampler,
            g_baseColorFactor,
            g_materialParams,
            g_textureFlags,
            g_lightDirection,
        };
        for (bgfx::UniformHandle uniform : uniforms) {
            if (bgfx::isValid(uniform)) {
                bgfx::destroy(uniform);
            }
        }

        if (bgfx::isValid(g_meshProgram)) {
            bgfx::destroy(g_meshProgram);
            g_meshProgram = BGFX_INVALID_HANDLE;
        }

        destroyTextures();
    }

    StaticMeshHandle loadStaticMesh(const std::filesystem::path& path)
    {
        const Assets::Assimp::ImportResult importResult = Assets::Assimp::importStaticMesh(path);
        if (!importResult.success) {
            SDL_Log("Failed to import static mesh %s: %s", path.string().c_str(), importResult.error.c_str());
            return {};
        }

        const std::filesystem::path meshDirectory = path.parent_path();
        std::vector<MaterialHandle> materialHandles;
        materialHandles.reserve(importResult.mesh.materials.size());

        for (const Assets::Assimp::ImportedMaterial& importedMaterial : importResult.mesh.materials) {
            MaterialResource material;
            material.baseColorFactor = importedMaterial.baseColorFactor;
            material.metallicFactor = importedMaterial.metallicFactor;
            material.roughnessFactor = importedMaterial.roughnessFactor;
            material.baseColorTexture = loadMaterialTexture(meshDirectory, importedMaterial.baseColorTexture, g_whiteTexture);
            material.normalTexture = loadMaterialTexture(meshDirectory, importedMaterial.normalTexture, g_flatNormalTexture);
            material.metallicTexture = loadMaterialTexture(meshDirectory, importedMaterial.metallicTexture, g_blackTexture);
            material.roughnessTexture = loadMaterialTexture(meshDirectory, importedMaterial.roughnessTexture, g_whiteTexture);
            materialHandles.push_back(addMaterial(material));
        }

        StaticMeshResource mesh;
        mesh.submeshes.reserve(importResult.mesh.submeshes.size());
        mesh.localBounds = emptyAabb();
        for (const Assets::Assimp::ImportedSubmesh& importedSubmesh : importResult.mesh.submeshes) {
            std::vector<MeshVertex> vertices;
            vertices.reserve(importedSubmesh.vertices.size());
            for (const Assets::Assimp::ImportedVertex& importedVertex : importedSubmesh.vertices) {
                vertices.push_back({
                    importedVertex.position.x,
                    importedVertex.position.y,
                    importedVertex.position.z,
                    importedVertex.normal.x,
                    importedVertex.normal.y,
                    importedVertex.normal.z,
                    importedVertex.tangent.x,
                    importedVertex.tangent.y,
                    importedVertex.tangent.z,
                    importedVertex.uv.x,
                    importedVertex.uv.y,
                });
                includePoint(mesh.localBounds, importedVertex.position);
                mesh.hasBounds = true;
            }

            SubmeshResource submesh;
            submesh.vertexBuffer = bgfx::createVertexBuffer(
                bgfx::copy(vertices.data(), static_cast<uint32_t>(vertices.size() * sizeof(MeshVertex))),
                MeshVertex::layout
            );
            submesh.indexBuffer = bgfx::createIndexBuffer(
                bgfx::copy(importedSubmesh.indices.data(), static_cast<uint32_t>(importedSubmesh.indices.size() * sizeof(uint32_t))),
                BGFX_BUFFER_INDEX32
            );
            submesh.indexCount = static_cast<uint32_t>(importedSubmesh.indices.size());
            submesh.material = importedSubmesh.materialIndex < materialHandles.size()
                ? materialHandles[importedSubmesh.materialIndex]
                : MaterialHandle{0};

            mesh.submeshes.push_back(submesh);
        }

        return addMesh(std::move(mesh));
    }

    StaticMeshHandle createTexturedCubeMesh()
    {
        const MeshVertex vertices[] = {
            {-1.0f,  1.0f,  1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f},
            { 1.0f,  1.0f,  1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f},
            {-1.0f, -1.0f,  1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f},
            { 1.0f, -1.0f,  1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f},
            {-1.0f,  1.0f, -1.0f, 0.0f, 0.0f,-1.0f,-1.0f, 0.0f, 0.0f, 1.0f, 0.0f},
            { 1.0f,  1.0f, -1.0f, 0.0f, 0.0f,-1.0f,-1.0f, 0.0f, 0.0f, 0.0f, 0.0f},
            {-1.0f, -1.0f, -1.0f, 0.0f, 0.0f,-1.0f,-1.0f, 0.0f, 0.0f, 1.0f, 1.0f},
            { 1.0f, -1.0f, -1.0f, 0.0f, 0.0f,-1.0f,-1.0f, 0.0f, 0.0f, 0.0f, 1.0f},
        };

        const uint32_t indices[] = {
            0, 2, 1, 1, 2, 3,
            4, 5, 6, 5, 7, 6,
            4, 6, 0, 0, 6, 2,
            1, 3, 5, 5, 3, 7,
            4, 0, 5, 5, 0, 1,
            2, 6, 3, 3, 6, 7,
        };

        SubmeshResource submesh;
        submesh.vertexBuffer = bgfx::createVertexBuffer(bgfx::copy(vertices, sizeof(vertices)), MeshVertex::layout);
        submesh.indexBuffer = bgfx::createIndexBuffer(bgfx::copy(indices, sizeof(indices)), BGFX_BUFFER_INDEX32);
        submesh.indexCount = static_cast<uint32_t>(std::size(indices));
        submesh.material = MaterialHandle{0};

        StaticMeshResource mesh;
        mesh.localBounds = {{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}};
        mesh.hasBounds = true;
        mesh.submeshes.push_back(submesh);
        return addMesh(std::move(mesh));
    }

    void destroyStaticMesh(StaticMeshHandle mesh)
    {
        if (!isValidStaticMesh(mesh)) {
            return;
        }

        for (MeshInstanceResource& instance : g_instances) {
            if (instance.alive && instance.mesh.id == mesh.id) {
                instance = {};
            }
        }

        StaticMeshResource& resource = g_meshes[mesh.id];
        for (SubmeshResource& submesh : resource.submeshes) {
            if (bgfx::isValid(submesh.vertexBuffer)) {
                bgfx::destroy(submesh.vertexBuffer);
            }
            if (bgfx::isValid(submesh.indexBuffer)) {
                bgfx::destroy(submesh.indexBuffer);
            }
        }
        resource = {};
    }

    MeshInstanceHandle createInstance(StaticMeshHandle mesh)
    {
        if (!isValidStaticMesh(mesh)) {
            return {};
        }

        for (uint32_t index = 0; index < g_instances.size(); ++index) {
            if (!g_instances[index].alive) {
                g_instances[index] = {};
                g_instances[index].alive = true;
                g_instances[index].mesh = mesh;
                return {index};
            }
        }

        const uint32_t id = static_cast<uint32_t>(g_instances.size());
        MeshInstanceResource instance;
        instance.alive = true;
        instance.mesh = mesh;
        g_instances.push_back(instance);
        return {id};
    }

    void destroyInstance(MeshInstanceHandle instance)
    {
        if (!isValidMeshInstance(instance)) {
            return;
        }

        g_instances[instance.id] = {};
    }

    TerrainHandle createTerrainTile(
        const std::vector<MeshVertex>& vertices,
        const std::vector<uint32_t>& indices,
        TextureHandle baseColorTexture)
    {
        if (vertices.empty() || indices.empty()) {
            return {};
        }

        TerrainTileResource terrain;
        terrain.alive = true;
        terrain.worldBounds = emptyAabb();
        for (const MeshVertex& vertex : vertices) {
            includePoint(terrain.worldBounds, {vertex.px, vertex.py, vertex.pz});
            terrain.hasBounds = true;
        }
        terrain.vertexBuffer = bgfx::createVertexBuffer(
            bgfx::copy(vertices.data(), static_cast<uint32_t>(vertices.size() * sizeof(MeshVertex))),
            MeshVertex::layout
        );
        terrain.indexBuffer = bgfx::createIndexBuffer(
            bgfx::copy(indices.data(), static_cast<uint32_t>(indices.size() * sizeof(uint32_t))),
            BGFX_BUFFER_INDEX32
        );
        terrain.indexCount = static_cast<uint32_t>(indices.size());
        terrain.baseColorTexture = Renderer::isValid(baseColorTexture) ? baseColorTexture : g_whiteTexture;

        for (uint32_t index = 0; index < g_terrainTiles.size(); ++index) {
            if (!g_terrainTiles[index].alive) {
                g_terrainTiles[index] = terrain;
                return {index};
            }
        }

        const uint32_t id = static_cast<uint32_t>(g_terrainTiles.size());
        g_terrainTiles.push_back(terrain);
        return {id};
    }

    void destroyTerrainTile(TerrainHandle terrain)
    {
        if (!isValidTerrain(terrain)) {
            return;
        }

        TerrainTileResource& resource = g_terrainTiles[terrain.id];
        if (bgfx::isValid(resource.vertexBuffer)) {
            bgfx::destroy(resource.vertexBuffer);
        }
        if (bgfx::isValid(resource.indexBuffer)) {
            bgfx::destroy(resource.indexBuffer);
        }
        resource = {};
    }

    void setInstancePosition(MeshInstanceHandle instance, const glm::vec3& position)
    {
        if (!isValidMeshInstance(instance)) {
            return;
        }
        g_instances[instance.id].position = position;
        g_instances[instance.id].usesExplicitTransform = false;
    }

    void setInstanceRotation(MeshInstanceHandle instance, const glm::vec3& eulerRadians)
    {
        if (!isValidMeshInstance(instance)) {
            return;
        }
        g_instances[instance.id].rotation = eulerRadians;
        g_instances[instance.id].usesExplicitTransform = false;
    }

    void setInstanceScale(MeshInstanceHandle instance, const glm::vec3& scale)
    {
        if (!isValidMeshInstance(instance)) {
            return;
        }
        g_instances[instance.id].scale = scale;
        g_instances[instance.id].usesExplicitTransform = false;
    }

    void setInstanceTransform(MeshInstanceHandle instance, const glm::mat4& transform)
    {
        if (!isValidMeshInstance(instance)) {
            return;
        }
        g_instances[instance.id].explicitTransform = transform;
        g_instances[instance.id].usesExplicitTransform = true;
    }

    void setInstanceBaseColorTexture(MeshInstanceHandle instance, TextureHandle texture)
    {
        if (!isValidMeshInstance(instance)) {
            return;
        }
        g_instances[instance.id].baseColorOverride = texture;
    }

    SceneDrawStats drawScene(const RenderView& view)
    {
        SceneDrawStats stats;
        if (!bgfx::isValid(g_meshProgram)) {
            return stats;
        }

        const Frustum frustum = makeFrustum(view.viewProjection);
        const glm::vec4 lightDirection = glm::normalize(glm::vec4{-0.35f, -0.85f, -0.25f, 0.0f});
        const glm::mat4 identity{1.0f};
        const glm::vec4 whiteFactor{1.0f};
        const glm::vec4 terrainMaterialParams{0.0f, 1.0f, 0.0f, 0.0f};
        const glm::vec4 noTextureFlags{0.0f};

        for (const TerrainTileResource& terrain : g_terrainTiles) {
            if (!terrain.alive ||
                !bgfx::isValid(terrain.vertexBuffer) ||
                !bgfx::isValid(terrain.indexBuffer)) {
                continue;
            }
            ++stats.liveTerrainTiles;
            if (terrain.hasBounds && !intersects(frustum, terrain.worldBounds)) {
                continue;
            }
            ++stats.visibleTerrainTiles;

            bgfx::setTransform(&identity[0][0]);
            bgfx::setUniform(g_baseColorFactor, &whiteFactor[0]);
            bgfx::setUniform(g_materialParams, &terrainMaterialParams[0]);
            bgfx::setUniform(g_textureFlags, &noTextureFlags[0]);
            bgfx::setUniform(g_lightDirection, &lightDirection[0]);
            bgfx::setTexture(0, g_baseColorSampler, getNativeTexture(terrain.baseColorTexture));
            bgfx::setTexture(1, g_normalSampler, getNativeTexture(g_flatNormalTexture));
            bgfx::setTexture(2, g_metallicSampler, getNativeTexture(g_blackTexture));
            bgfx::setTexture(3, g_roughnessSampler, getNativeTexture(g_whiteTexture));
            bgfx::setVertexBuffer(0, terrain.vertexBuffer);
            bgfx::setIndexBuffer(terrain.indexBuffer, 0, terrain.indexCount);
            bgfx::setState(BGFX_STATE_DEFAULT | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
            bgfx::submit(view.viewId, g_meshProgram);
            ++stats.submittedTerrainTiles;
        }

        for (const MeshInstanceResource& instance : g_instances) {
            if (!instance.alive || !isValidStaticMesh(instance.mesh)) {
                continue;
            }
            ++stats.liveMeshInstances;

            const glm::mat4 transform = composeTransform(instance);
            const StaticMeshResource& mesh = g_meshes[instance.mesh.id];
            if (mesh.hasBounds && !intersects(frustum, transformAabb(mesh.localBounds, transform))) {
                continue;
            }
            ++stats.visibleMeshInstances;

            bgfx::setTransform(&transform[0][0]);

            bool submittedInstance = false;
            for (const SubmeshResource& submesh : mesh.submeshes) {
                if (!bgfx::isValid(submesh.vertexBuffer) ||
                    !bgfx::isValid(submesh.indexBuffer) ||
                    !isValidMaterial(submesh.material)) {
                    continue;
                }

                const MaterialResource& material = g_materials[submesh.material.id];
                const TextureHandle baseColorTexture = Renderer::isValid(instance.baseColorOverride)
                    ? instance.baseColorOverride
                    : material.baseColorTexture;
                const TextureHandle normalTexture = Renderer::isValid(material.normalTexture) ? material.normalTexture : g_flatNormalTexture;
                const TextureHandle metallicTexture = Renderer::isValid(material.metallicTexture) ? material.metallicTexture : g_blackTexture;
                const TextureHandle roughnessTexture = Renderer::isValid(material.roughnessTexture) ? material.roughnessTexture : g_whiteTexture;

                const glm::vec4 materialParams{
                    material.metallicFactor,
                    material.roughnessFactor,
                    metallicTexture.id != g_blackTexture.id ? 1.0f : 0.0f,
                    roughnessTexture.id != g_whiteTexture.id ? 1.0f : 0.0f,
                };
                const glm::vec4 textureFlags{
                    normalTexture.id != g_flatNormalTexture.id ? 1.0f : 0.0f,
                    0.0f,
                    0.0f,
                    0.0f,
                };

                bgfx::setUniform(g_baseColorFactor, &material.baseColorFactor[0]);
                bgfx::setUniform(g_materialParams, &materialParams[0]);
                bgfx::setUniform(g_textureFlags, &textureFlags[0]);
                bgfx::setUniform(g_lightDirection, &lightDirection[0]);

                bgfx::setTexture(0, g_baseColorSampler, getNativeTexture(baseColorTexture));
                bgfx::setTexture(1, g_normalSampler, getNativeTexture(normalTexture));
                bgfx::setTexture(2, g_metallicSampler, getNativeTexture(metallicTexture));
                bgfx::setTexture(3, g_roughnessSampler, getNativeTexture(roughnessTexture));
                bgfx::setVertexBuffer(0, submesh.vertexBuffer);
                bgfx::setIndexBuffer(submesh.indexBuffer, 0, submesh.indexCount);
                bgfx::setState(BGFX_STATE_DEFAULT | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
                bgfx::submit(view.viewId, g_meshProgram);
                submittedInstance = true;
            }

            if (submittedInstance) {
                ++stats.submittedMeshInstances;
            }
        }

        return stats;
    }
}
