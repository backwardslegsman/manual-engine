    std::filesystem::path absolutePathWithoutThrow(const std::filesystem::path& path)
    {
        std::error_code error;
        std::filesystem::path absolute = std::filesystem::absolute(path, error);
        return error ? path : absolute;
    }

    std::filesystem::path normalizedPathForLookup(const std::filesystem::path& path)
    {
        std::error_code error;
        std::filesystem::path normalized = std::filesystem::weakly_canonical(path, error);
        return error ? absolutePathWithoutThrow(path) : normalized;
    }

    void appendUniquePathCandidate(
        std::vector<std::filesystem::path>& candidates,
        const std::filesystem::path& candidate)
    {
        if (candidate.empty()) {
            return;
        }

        const std::filesystem::path normalized = normalizedPathForLookup(candidate);
        if (std::find(candidates.begin(), candidates.end(), normalized) == candidates.end()) {
            candidates.push_back(normalized);
        }
    }

    void appendRelativePathSearch(
        std::vector<std::filesystem::path>& candidates,
        const std::filesystem::path& base,
        const std::filesystem::path& relativePath)
    {
        if (base.empty() || relativePath.empty()) {
            return;
        }

        std::filesystem::path cursor = absolutePathWithoutThrow(base);
        for (int depth = 0; depth < 8 && !cursor.empty(); ++depth) {
            appendUniquePathCandidate(candidates, cursor / relativePath);

            const std::filesystem::path parent = cursor.parent_path();
            if (parent == cursor) {
                break;
            }
            cursor = parent;
        }
    }

    std::filesystem::path executableBasePath()
    {
        const char* basePath = SDL_GetBasePath();
        return basePath ? std::filesystem::path{basePath} : std::filesystem::path{};
    }

    std::filesystem::path resolveAuthoredScenePath(const std::filesystem::path& requestedPath)
    {
        if (requestedPath.empty() || requestedPath.is_absolute()) {
            return requestedPath;
        }

        std::vector<std::filesystem::path> candidates;
        appendUniquePathCandidate(candidates, requestedPath);

        std::error_code error;
        const std::filesystem::path currentPath = std::filesystem::current_path(error);
        if (!error) {
            appendRelativePathSearch(candidates, currentPath, requestedPath);
        }
        appendRelativePathSearch(candidates, executableBasePath(), requestedPath);

        for (const std::filesystem::path& candidate : candidates) {
            std::error_code existsError;
            if (std::filesystem::exists(candidate, existsError) && !existsError) {
                return candidate;
            }
        }

        return candidates.empty() ? requestedPath : candidates.front();
    }

    struct AuthoredFallbackScene {
        Renderer::StaticMeshHandle mesh;
        Renderer::MeshInstanceHandle instance;

        void shutdown()
        {
            Renderer::destroyInstance(instance);
            Renderer::destroyStaticMesh(mesh);
            instance = {};
            mesh = {};
        }
    };

    struct ModernTerrainChunkRuntime {
        Engine::TerrainChunkHandle chunk;
        Engine::ChunkCoord coord;
        Renderer::TerrainHandle rendererTerrain;
        Engine::NavigationTileHandle navigationTile;
        Engine::TerrainPhysicsColliderHandle physicsCollider;
    };

    struct ModernAuthoredAssetRuntime {
        Engine::SceneAuthoredAdapterResult result;
        std::filesystem::path path;
        std::string label;
    };

    struct ModernAnimatedAssetRuntime {
        Engine::SceneAnimatedAdapterResult result;
        std::filesystem::path path;
        std::string label;
    };

    struct ModernDefaultSceneRuntime {
        Engine::Scene scene;
        Engine::SceneRenderBridge renderBridge{scene};
        Engine::RendererSceneRenderBackend renderBackend;
        Engine::ScenePhysicsWorld physics{scene};
        Engine::NavigationSystem navigation;
        Engine::NavigationConnectivitySystem navigationConnectivity;
        Engine::SceneNavigationService navigationService{navigation, &navigationConnectivity};
        Engine::SceneNavigationGeometryRegistry sceneNavigationGeometry;
        Engine::SceneCharacterMovementSystem characters{scene, physics};
        Engine::SceneAnimatedModelAdapter animatedAdapter{scene, renderBridge};
        Engine::TerrainDataset terrain;
        Engine::TerrainPhysicsColliderAdapter terrainPhysics;
        Engine::OpenWorldStreamingRuntime streamingRuntime;
        Engine::AsyncWorkQueue streamingAsync;
        Engine::MainThreadWorkQueue streamingMainThread;
        Engine::FrameBudget streamingBudget;
        Engine::TerrainMaterialRenderBinding terrainMaterial;
        Engine::TerrainSourceHandle terrainSource;
        Engine::SceneActorHandle playerActor;
        Engine::SceneCharacterHandle playerCharacter;
        std::vector<ModernTerrainChunkRuntime> terrainChunks;
        std::vector<ModernAuthoredAssetRuntime> authoredAssets;
        std::vector<ModernAnimatedAssetRuntime> animatedAssets;
        std::vector<Engine::ScenePhysicsBodyHandle> authoredPhysicsBodies;
        std::unordered_map<uint64_t, Engine::TerrainChunkHandle> streamingTerrainChunks;
        std::unordered_map<uint64_t, Renderer::TerrainHandle> streamingTerrainHandles;
        std::unordered_map<uint64_t, Engine::NavigationTileHandle> streamingNavigationTiles;
        std::unordered_map<uint64_t, Engine::TerrainPhysicsColliderHandle> streamingPhysicsColliders;
        uint64_t nextStreamingToken = 1;
        Engine::CachedTexture terrainFallbackTexture;
        Renderer::MaterialHandle terrainFallbackMaterial;
        Renderer::Aabb bounds{{-64.0f, -8.0f, -64.0f}, {64.0f, 32.0f, 64.0f}};
        glm::vec3 focus{0.0f, 0.0f, 0.0f};
        std::string status = "Modern scene not loaded.";
        uint32_t terrainRendererCount = 0;
        uint32_t terrainNavTileCount = 0;
        uint32_t terrainPhysicsColliderCount = 0;
        uint32_t authoredNavigationSourceCount = 0;
        uint32_t authoredPhysicsBodyCount = 0;
        uint32_t navigationCacheHitCount = 0;
        uint32_t navigationCacheMissCount = 0;
        uint32_t navigationCacheStaleCount = 0;
        uint32_t navigationCacheWriteCount = 0;
        uint32_t staticAssetCount = 0;
        uint32_t animatedAssetCount = 0;
        uint32_t warningCount = 0;
        bool loadedHeightmap = false;
        bool usingOpenWorldStreaming = false;
        bool schedulerStarted = false;
        bool cursorTraceDebugVisible = false;
        Engine::CursorWorldRay lastCursorTraceRay;
        Engine::CursorNavigationProjectionResult lastCursorTraceProjection;
        std::string navigationStatus = "Navigation connectivity has not been rebuilt yet.";

        void shutdown(Engine::AssetCache& assetCache)
        {
            if (scene.lifecycleState() == Engine::SceneLifecycleState::Started) {
                scene.stop();
            }
            for (auto& [_, terrain] : streamingTerrainHandles) {
                Renderer::destroyTerrainTile(terrain);
            }
            streamingTerrainHandles.clear();
            for (auto& [_, tile] : streamingNavigationTiles) {
                (void)tile;
            }
            navigationConnectivity.clear();
            navigation.clear();
            streamingNavigationTiles.clear();
            for (auto& [_, collider] : streamingPhysicsColliders) {
                terrainPhysics.destroyCollider(scene, physics, collider);
            }
            streamingPhysicsColliders.clear();
            for (auto& [_, chunk] : streamingTerrainChunks) {
                terrain.unloadChunk(chunk);
            }
            streamingTerrainChunks.clear();
            streamingRuntime.shutdown();
            streamingAsync.shutdown();
            streamingMainThread.clear();
            renderBridge.releaseRendererResources(renderBackend);
            for (Engine::ScenePhysicsBodyHandle body : authoredPhysicsBodies) {
                physics.destroyBody(body);
            }
            authoredPhysicsBodies.clear();
            terrainPhysics.releaseAll(scene, physics);
            for (ModernTerrainChunkRuntime& chunk : terrainChunks) {
                Renderer::destroyTerrainTile(chunk.rendererTerrain);
                navigation.destroyTile(chunk.coord);
                chunk = {};
            }
            terrainChunks.clear();
            for (ModernAnimatedAssetRuntime& asset : animatedAssets) {
                animatedAdapter.releaseResources(asset.result.resources, assetCache);
            }
            animatedAssets.clear();
            for (ModernAuthoredAssetRuntime& asset : authoredAssets) {
                Engine::releaseSceneAuthoredAdapterResources(asset.result.resources, assetCache);
            }
            authoredAssets.clear();
            animatedAdapter.unregisterVariableAnimationSystem();
            renderBridge.unregisterPreRenderSystem();
            physics.unregisterPhysicsSystems();
            characters.unregisterMovementSystem();
            Engine::releaseTerrainMaterialResources(assetCache, terrainMaterial);
            assetCache.release(terrainFallbackTexture);
            if (scene.lifecycleState() == Engine::SceneLifecycleState::Loaded) {
                scene.unload();
            }
            schedulerStarted = false;
        }
    };

    void includePoint(Renderer::Aabb& bounds, const glm::vec3& point)
    {
        bounds.min = glm::min(bounds.min, point);
        bounds.max = glm::max(bounds.max, point);
    }

    void includeBounds(Renderer::Aabb& bounds, const Renderer::Aabb& other)
    {
        includePoint(bounds, other.min);
        includePoint(bounds, other.max);
    }

    uint64_t modernHashText(std::string_view text, uint64_t hash = 14695981039346656037ull)
    {
        for (const char value : text) {
            hash ^= static_cast<uint8_t>(value);
            hash *= 1099511628211ull;
        }
        return hash;
    }

    Engine::AssetId modernAssetIdForPath(const std::filesystem::path& path, std::string_view salt)
    {
        uint64_t hash = modernHashText(path.lexically_normal().generic_string());
        hash = modernHashText(salt, hash);
        return {hash == 0 ? 1 : hash};
    }

    Engine::TerrainRenderLodSourceIdentity modernTerrainRenderIdentity(
        const Engine::TerrainSourceDescriptor& descriptor)
    {
        return {
            descriptor.sourceId,
            descriptor.debugName + ".render",
            descriptor.settings,
            descriptor.type,
        };
    }

    Engine::TerrainNavigationSourceIdentity modernTerrainNavigationIdentity(
        const Engine::TerrainSourceDescriptor& descriptor)
    {
        return {
            descriptor.sourceId,
            descriptor.debugName + ".navigation",
            descriptor.settings,
            descriptor.type,
        };
    }

    Engine::TerrainPhysicsSourceIdentity modernTerrainPhysicsIdentity(
        const Engine::TerrainSourceDescriptor& descriptor)
    {
        return {
            descriptor.sourceId,
            descriptor.debugName + ".physics",
            descriptor.settings,
            descriptor.type,
        };
    }

    std::string modernTerrainSourceTypeName(Engine::TerrainDatasetSourceType type)
    {
        switch (type) {
            case Engine::TerrainDatasetSourceType::HeightmapImported:
                return "heightmap_imported";
            case Engine::TerrainDatasetSourceType::Procedural:
                return "procedural";
            case Engine::TerrainDatasetSourceType::Generated:
                return "generated";
        }
        return "unknown";
    }

    uint64_t modernHashFloat(float value, uint64_t hash)
    {
        return modernHashText(std::to_string(value), hash);
    }

    std::string modernSceneGeometryHash(ModernDefaultSceneRuntime& runtime)
    {
        uint64_t hash = modernHashText("modern_scene_nav_geometry_slope_v1");
        for (const ModernAuthoredAssetRuntime& asset : runtime.authoredAssets) {
            hash = modernHashText(asset.path.lexically_normal().generic_string(), hash);
            hash = modernHashText(Engine::NavigationCache::hashFile(asset.path), hash);
            hash = modernHashText(asset.label, hash);
            hash = modernHashText(std::to_string(asset.result.diagnostics.createdNavigationSourceCount), hash);
            for (const Engine::SceneAuthoredNodeBinding& node : asset.result.nodes) {
                for (Engine::SceneNavigationSourceHandle source : node.navigationSources) {
                    const std::optional<Engine::SceneNavigationSourceDescriptor> descriptor =
                        runtime.sceneNavigationGeometry.descriptor(source);
                    if (!descriptor) {
                        continue;
                    }
                    hash = modernHashText(descriptor->debugName, hash);
                    hash = modernHashText(std::to_string(static_cast<uint32_t>(descriptor->type)), hash);
                    hash = modernHashText(std::to_string(static_cast<uint32_t>(descriptor->role)), hash);
                    hash = modernHashText(std::to_string(descriptor->vertices.size()), hash);
                    hash = modernHashText(std::to_string(descriptor->indices.size()), hash);
                    for (const glm::vec3& vertex : descriptor->vertices) {
                        hash = modernHashFloat(vertex.x, hash);
                        hash = modernHashFloat(vertex.y, hash);
                        hash = modernHashFloat(vertex.z, hash);
                    }
                    if (descriptor->actor.has_value()) {
                        const std::optional<glm::mat4> world = runtime.scene.worldMatrix(*descriptor->actor);
                        if (world) {
                            for (int column = 0; column < 4; ++column) {
                                for (int row = 0; row < 4; ++row) {
                                    hash = modernHashFloat((*world)[column][row], hash);
                                }
                            }
                        }
                    }
                }
            }
        }
        return std::to_string(hash == 0 ? 1 : hash);
    }

    void placeSceneRoots(
        Engine::Scene& scene,
        const std::vector<Engine::SceneAuthoredNodeBinding>& nodes,
        const glm::vec3& translation,
        float scale)
    {
        for (const Engine::SceneAuthoredNodeBinding& binding : nodes) {
            if (!scene.contains(binding.actor) || scene.parent(binding.actor).has_value()) {
                continue;
            }
            std::optional<Engine::SceneTransform> transform = scene.localTransform(binding.actor);
            if (!transform) {
                continue;
            }
            transform->translation += translation;
            transform->scale *= scale;
            scene.setLocalTransform(binding.actor, *transform);
        }
    }

    void placeSceneRoots(
        Engine::Scene& scene,
        const std::vector<Engine::SceneAnimatedNodeBinding>& nodes,
        const glm::vec3& translation,
        float scale)
    {
        for (const Engine::SceneAnimatedNodeBinding& binding : nodes) {
            if (!scene.contains(binding.actor) || scene.parent(binding.actor).has_value()) {
                continue;
            }
            std::optional<Engine::SceneTransform> transform = scene.localTransform(binding.actor);
            if (!transform) {
                continue;
            }
            transform->translation += translation;
            transform->scale *= scale;
            scene.setLocalTransform(binding.actor, *transform);
        }
    }

    void createModernAuthoredPhysics(
        ModernDefaultSceneRuntime& runtime,
        const Engine::SceneAuthoredAdapterResult& result,
        std::string_view label)
    {
        for (const Engine::SceneAuthoredNodeBinding& binding : result.nodes) {
            for (Engine::SceneNavigationSourceHandle source : binding.navigationSources) {
                const std::optional<Engine::SceneNavigationSourceDescriptor> descriptor =
                    runtime.sceneNavigationGeometry.descriptor(source);
                if (!descriptor ||
                    descriptor->vertices.empty() ||
                    descriptor->indices.size() < 3 ||
                    !descriptor->actor.has_value()) {
                    ++runtime.warningCount;
                    continue;
                }

                Engine::ScenePhysicsBodyDescriptor bodyDescriptor;
                bodyDescriptor.actor = *descriptor->actor;
                bodyDescriptor.motionType = Engine::ScenePhysicsMotionType::Static;
                bodyDescriptor.enabled = true;
                const Engine::ScenePhysicsBodyHandle body = runtime.physics.createBody(bodyDescriptor);
                if (!Engine::isValid(body)) {
                    ++runtime.warningCount;
                    continue;
                }

                Engine::ScenePhysicsShapeDescriptor shape;
                shape.type = Engine::ScenePhysicsShapeType::StaticTriangleMesh;
                shape.triangleMesh.vertices = descriptor->vertices;
                shape.triangleMesh.indices = descriptor->indices;
                const Engine::SceneColliderHandle collider = runtime.physics.attachCollider(body, shape);
                if (!Engine::isValid(collider)) {
                    runtime.physics.destroyBody(body);
                    ++runtime.warningCount;
                    continue;
                }

                runtime.authoredPhysicsBodies.push_back(body);
                ++runtime.authoredPhysicsBodyCount;
            }
        }

        if (result.diagnostics.createdNavigationSourceCount > 0) {
            SDL_Log("Modern static asset '%.*s' registered %u nav sources and %u total authored physics bodies.",
                static_cast<int>(label.size()),
                label.data(),
                result.diagnostics.createdNavigationSourceCount,
                runtime.authoredPhysicsBodyCount);
        }
    }

    bool addModernAuthoredAsset(
        ModernDefaultSceneRuntime& runtime,
        Engine::AssetCache& assetCache,
        const std::filesystem::path& path,
        std::string label,
        const glm::vec3& translation,
        float scale)
    {
        const std::filesystem::path resolvedPath = resolveAuthoredScenePath(path);
        if (!std::filesystem::exists(resolvedPath)) {
            SDL_Log("Modern scene static asset missing: %s", resolvedPath.generic_string().c_str());
            ++runtime.warningCount;
            return false;
        }
        const Assets::Assimp::ImportedScene imported = Assets::Assimp::importScene(resolvedPath);
        if (!imported.success) {
            SDL_Log("Modern scene static asset import failed: %s: %s",
                resolvedPath.generic_string().c_str(),
                imported.error.c_str());
            ++runtime.warningCount;
            return false;
        }

        Engine::SceneAuthoredAdapterSettings settings;
        settings.loadTextures = true;
        settings.renderLayer = Renderer::RenderLayer::Props;
        settings.materialNamePrefix = "ModernStatic." + label;
        settings.textureDebugNamePrefix = "ModernStatic." + label;
        settings.navigationGeometry = &runtime.sceneNavigationGeometry;
        settings.registerNavigationSources = true;
        Engine::SceneAuthoredAdapterResult result = Engine::adaptImportedSceneToScene(
            imported,
            resolvedPath,
            assetCache,
            runtime.scene,
            runtime.renderBridge,
            settings);
        if (!result.success) {
            SDL_Log("Modern scene static asset adaptation failed: %s: %s",
                resolvedPath.generic_string().c_str(),
                result.message.c_str());
            Engine::releaseSceneAuthoredAdapterResources(result.resources, assetCache);
            ++runtime.warningCount;
            return false;
        }

        placeSceneRoots(runtime.scene, result.nodes, translation, scale);
        runtime.authoredNavigationSourceCount += result.diagnostics.createdNavigationSourceCount;
        createModernAuthoredPhysics(runtime, result, label);
        runtime.staticAssetCount += 1;
        runtime.warningCount += static_cast<uint32_t>(result.diagnostics.warnings.size());
        runtime.authoredAssets.push_back({std::move(result), resolvedPath, std::move(label)});
        return true;
    }

    bool addModernAnimatedAsset(
        ModernDefaultSceneRuntime& runtime,
        Engine::AssetCache& assetCache,
        const std::filesystem::path& path,
        std::string label,
        const glm::vec3& translation,
        float scale)
    {
        const std::filesystem::path resolvedPath = resolveAuthoredScenePath(path);
        if (!std::filesystem::exists(resolvedPath)) {
            SDL_Log("Modern scene animated asset missing: %s", resolvedPath.generic_string().c_str());
            ++runtime.warningCount;
            return false;
        }
        const Assets::Assimp::ImportedScene imported = Assets::Assimp::importScene(resolvedPath);
        if (!imported.success) {
            SDL_Log("Modern scene animated asset import failed: %s: %s",
                resolvedPath.generic_string().c_str(),
                imported.error.c_str());
            ++runtime.warningCount;
            return false;
        }

        Engine::SceneAnimatedAdapterSettings settings;
        settings.loadTextures = true;
        settings.renderLayer = Renderer::RenderLayer::Props;
        settings.allowRootFallbackSkinnedBindings = true;
        settings.materialNamePrefix = "ModernAnimated." + label;
        settings.textureDebugNamePrefix = "ModernAnimated." + label;
        Engine::SceneAnimatedAdapterResult result = runtime.animatedAdapter.adaptImportedScene(
            imported,
            resolvedPath,
            assetCache,
            settings);
        if (!result.success) {
            SDL_Log("Modern scene animated asset adaptation failed: %s: %s",
                resolvedPath.generic_string().c_str(),
                result.message.c_str());
            runtime.animatedAdapter.releaseResources(result.resources, assetCache);
            ++runtime.warningCount;
            return false;
        }

        placeSceneRoots(runtime.scene, result.nodes, translation, scale);
        const bool shouldUseAsPlayer =
            !Engine::isValid(runtime.playerActor) &&
            !result.nodes.empty() &&
            label == "kaykit_skinned_gltf";
        if (shouldUseAsPlayer) {
            runtime.playerActor = result.nodes.front().actor;
        }
        runtime.animatedAssetCount += 1;
        runtime.warningCount += static_cast<uint32_t>(result.diagnostics.warnings.size());
        runtime.animatedAssets.push_back({std::move(result), resolvedPath, std::move(label)});
        return true;
    }

    glm::vec3 modernTerrainRelativePosition(
        const ModernDefaultSceneRuntime& runtime,
        const glm::vec3& focus,
        const glm::vec3& offset)
    {
        glm::vec3 position{focus.x + offset.x, focus.y, focus.z + offset.z};
        position.y = runtime.terrain.sampleHeight(position.x, position.z).value_or(focus.y) + offset.y;
        return position;
    }

    bool resetModernPlayerAboveTerrain(
        ModernDefaultSceneRuntime& runtime,
        float verticalOffset = 24.0f);

    std::string requestModernCharacterNavigationTest(
        ModernDefaultSceneRuntime& runtime,
        const Engine::CursorWorldRay& cursorRay)
    {
        runtime.cursorTraceDebugVisible = true;
        runtime.lastCursorTraceRay = cursorRay;
        runtime.lastCursorTraceProjection = {};
        runtime.lastCursorTraceProjection.ray = cursorRay;

        if (!Engine::isValid(runtime.playerCharacter)) {
            ++runtime.warningCount;
            return "Cannot request navigation test path: player character is invalid.";
        }
        const bool hasTerrainCollider = runtime.terrainPhysicsColliderCount > 0 ||
            !runtime.streamingPhysicsColliders.empty();
        if (!hasTerrainCollider) {
            runtime.characters.setEnabled(runtime.playerCharacter, false);
            ++runtime.warningCount;
            return "Cannot request navigation test path: no live terrain physics collider is loaded for character movement yet.";
        }
        if (const std::optional<Engine::SceneCharacterDescriptor> character =
                runtime.characters.descriptor(runtime.playerCharacter);
            character && !character->enabled) {
            runtime.characters.setEnabled(runtime.playerCharacter, true);
            (void)resetModernPlayerAboveTerrain(runtime, 4.0f);
        }
        if (runtime.navigation.allTileDiagnostics().empty()) {
            ++runtime.warningCount;
            return "Cannot request navigation test path: no live navigation tiles are loaded.";
        }
        if (runtime.navigationConnectivity.stats().chunkCount == 0) {
            runtime.navigationStatus =
                rebuildModernNavigationConnectivityFromLoadedTiles(runtime.navigationConnectivity, runtime.navigation);
        }

        Engine::NavigationQueryFilter filter;
        filter.requireLoadedTiles = true;
        filter.allowPartialPath = false;
        filter.captureDebug = true;

        const Engine::CursorNavigationProjectionResult projected =
            Engine::projectCursorRayToNavigation(
                runtime.navigationService,
                cursorRay,
                {},
                filter,
                2048.0f,
                1.0f);
        runtime.lastCursorTraceProjection = projected;
        SDL_Log(
            "[ModernNavigationDebug] cursor ray status=%s origin=(%.3f,%.3f,%.3f) dir=(%.3f,%.3f,%.3f) projectedStatus=%s samples=%u sampled=(%.3f,%.3f,%.3f) projected=(%.3f,%.3f,%.3f)",
            Engine::cursorTraceStatusName(cursorRay.status),
            cursorRay.origin.x,
            cursorRay.origin.y,
            cursorRay.origin.z,
            cursorRay.direction.x,
            cursorRay.direction.y,
            cursorRay.direction.z,
            Engine::cursorTraceStatusName(projected.status),
            projected.sampleCount,
            projected.sampledPoint.x,
            projected.sampledPoint.y,
            projected.sampledPoint.z,
            projected.projection.point.x,
            projected.projection.point.y,
            projected.projection.point.z);
        if (projected.status != Engine::CursorTraceStatus::Success) {
            ++runtime.warningCount;
            return "Cannot request navigation test path: cursor did not project onto loaded navmesh: " +
                projected.message + " screen=(" +
                std::to_string(static_cast<int>(cursorRay.screenPosition.x)) + "," +
                std::to_string(static_cast<int>(cursorRay.screenPosition.y)) + ") ray=" +
                Engine::cursorTraceStatusName(cursorRay.status) + " samples=" +
                std::to_string(projected.sampleCount) + ".";
        }

        Engine::SceneCharacterPathRequest request;
        request.goal = projected.projection.point;
        request.filter = filter;
        request.waypointAcceptanceRadius = 0.75f;
        request.allowPartialPath = false;

        if (!runtime.characters.requestPathTo(runtime.playerCharacter, request)) {
            const std::optional<Engine::SceneCharacterState> state =
                runtime.characters.state(runtime.playerCharacter);
            ++runtime.warningCount;
            return state ? "Navigation test path request failed: " + state->lastMessage
                         : "Navigation test path request failed.";
        }

        const std::optional<Engine::SceneCharacterState> state =
            runtime.characters.state(runtime.playerCharacter);
        if (const std::optional<glm::mat4> playerWorld = runtime.scene.worldMatrix(runtime.playerActor)) {
            const glm::vec3 playerPosition{(*playerWorld)[3]};
            SDL_Log(
                "[ModernNavigationDebug] path accepted player=(%.3f,%.3f,%.3f) goal=(%.3f,%.3f,%.3f) points=%u activeWaypoint=%u status=%d message=%s",
                playerPosition.x,
                playerPosition.y,
                playerPosition.z,
                request.goal.x,
                request.goal.y,
                request.goal.z,
                state ? static_cast<uint32_t>(state->activePath.points.size()) : 0u,
                state ? state->activeWaypointIndex : 0u,
                state ? static_cast<int>(state->lastStatus) : -1,
                state ? state->lastMessage.c_str() : "No character state");
        }
        return "Navigation test path accepted to cursor-projected goal; path points " +
            std::to_string(state ? state->activePath.points.size() : 0u) + ".";
    }

    Engine::SceneCharacterHandle createModernPlayerCharacter(ModernDefaultSceneRuntime& runtime)
    {
        Engine::SceneCharacterDescriptor character;
        character.actor = runtime.playerActor;
        character.radius = 0.45f;
        character.height = 1.8f;
        character.maxSpeed = 7.0f;
        character.snapDistance = 10000.0f;
        character.debugName = "modern.player";
        return runtime.characters.createCharacter(character);
    }

    bool resetModernPlayerAboveTerrain(
        ModernDefaultSceneRuntime& runtime,
        float verticalOffset)
    {
        if (!Engine::isValid(runtime.playerActor)) {
            return false;
        }
        std::optional<Engine::SceneTransform> transform = runtime.scene.localTransform(runtime.playerActor);
        if (!transform.has_value()) {
            return false;
        }
        const float terrainY = runtime.terrain.sampleHeight(transform->translation.x, transform->translation.z)
            .value_or(runtime.focus.y);
        transform->translation.y = terrainY + verticalOffset;
        runtime.scene.setLocalTransform(runtime.playerActor, *transform);
        if (const std::optional<Engine::ScenePhysicsBodyHandle> body =
                runtime.physics.bodyForActor(runtime.playerActor)) {
            runtime.physics.setKinematicTarget(*body, transform->translation, transform->rotation);
        }
        if (!Engine::isValid(runtime.playerCharacter)) {
            runtime.playerCharacter = createModernPlayerCharacter(runtime);
        }
        return Engine::isValid(runtime.playerCharacter);
    }

    Engine::TerrainSourceDescriptor modernFallbackTerrainSourceDescriptor()
    {
        Engine::TerrainSourceDescriptor descriptor;
        descriptor.sourceId = {0x5445525241494e1ull};
        descriptor.type = Engine::TerrainDatasetSourceType::Procedural;
        descriptor.bounds = {{-96.0f, -16.0f, -96.0f}, {96.0f, 32.0f, 96.0f}};
        descriptor.defaultChunkSize = 64.0f;
        descriptor.defaultResolution = 33;
        descriptor.debugName = "modern.default.procedural_fallback";
        descriptor.procedural.origin = {-96.0f, 0.0f, 96.0f};
        descriptor.procedural.chunkSize = descriptor.defaultChunkSize;
        descriptor.procedural.resolution = descriptor.defaultResolution;
        descriptor.procedural.heightScale = 8.0f;
        return descriptor;
    }

    Engine::OpenWorldStreamingRuntimeSettings modernStreamingRuntimeSettings(
        std::string sceneGeometryHash = {},
        const ManualEngine::App::EditorProjectSettings* editorSettings = nullptr)
    {
        Engine::OpenWorldStreamingRuntimeSettings settings =
            editorSettings
                ? ManualEngine::App::streamingRuntimeSettingsFromEditorProject(*editorSettings)
                : ManualEngine::App::defaultEditorProjectSettings().streaming;
        const std::filesystem::path heightmapPath = resolveAuthoredScenePath(settings.bake.heightmap.sourcePath);
        settings.bake.heightmap.sourcePath = heightmapPath;
        if (!settings.bake.heightmap.sourceIdOverride) {
            settings.bake.heightmap.sourceIdOverride =
                modernAssetIdForPath(heightmapPath, "modern_heightmap_streaming");
        }
        settings.bake.sceneGeometryHash = std::move(sceneGeometryHash);
        return settings;
    }

    Renderer::Aabb boundsFromStreamingManifest(const Engine::StreamingChunkManifest& manifest)
    {
        Renderer::Aabb bounds{
            {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()},
            {-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()},
        };
        for (const Engine::StreamingChunkManifestRecord& record : manifest.records) {
            if (record.payload == Engine::StreamingPayloadKind::TerrainChunk) {
                includeBounds(bounds, {record.bounds.min, record.bounds.max});
            }
        }
        if (bounds.min.x > bounds.max.x) {
            return {{-64.0f, -8.0f, -64.0f}, {64.0f, 32.0f, 64.0f}};
        }
        return bounds;
    }

    Engine::TerrainSourceDescriptor modernStreamingTerrainSourceDescriptor(
        const Engine::OpenWorldStreamingRuntimeSettings& settings)
    {
        Engine::TerrainSourceDescriptor source;
        source.sourceId = settings.bake.heightmap.sourceIdOverride.value_or(modernAssetIdForPath(
            settings.bake.heightmap.sourcePath,
            "modern_heightmap_streaming"));
        source.type = Engine::TerrainDatasetSourceType::HeightmapImported;
        source.bounds = {{-256.0f, -80.0f, -256.0f}, {256.0f, 80.0f, 256.0f}};
        source.defaultChunkSize = settings.bake.heightmap.chunkWorldSize;
        source.defaultResolution = settings.bake.heightmap.chunkResolution;
        source.settings = Engine::terrainHeightmapImportSettingsKey(settings.bake.heightmap);
        source.debugName = "modern.default.streaming_heightmap";
        return source;
    }

    const Engine::StreamingReadDescriptorEntry* modernStreamingDescriptor(
        const ModernDefaultSceneRuntime& runtime,
        const Engine::StreamingPromotionRequest& request)
    {
        return Engine::findStreamingReadDescriptor(
            runtime.streamingRuntime.readDescriptors(),
            request.key,
            request.payload);
    }

    Renderer::MeshVertex meshVertexFromTerrainCpu(const Engine::TerrainCpuMeshVertex& vertex)
    {
        return {
            vertex.position.x,
            vertex.position.y,
            vertex.position.z,
            vertex.normal.x,
            vertex.normal.y,
            vertex.normal.z,
            vertex.tangent.x,
            vertex.tangent.y,
            vertex.tangent.z,
            vertex.tangent.w,
            vertex.uv0.x,
            vertex.uv0.y,
            vertex.uv1.x,
            vertex.uv1.y,
            vertex.color,
        };
    }

    Engine::StreamingPromotionCallbacks modernStreamingCallbacks(ModernDefaultSceneRuntime& runtime)
    {
        Engine::StreamingPromotionCallbacks callbacks;
        callbacks.promote = [&runtime](
            const Engine::StreamingPromotionRequest& request,
            const Engine::StreamingCachedPayload& payload) {
            Engine::StreamingPromotionResult result;
            result.status = Engine::StreamingPromotionStatus::UnsupportedPayload;
            result.message = "Unsupported modern default streaming payload.";
            const uint64_t token = runtime.nextStreamingToken++;
            result.liveToken = {token};

            if (request.payload == Engine::StreamingPayloadKind::TerrainChunk) {
                const auto* terrain = std::get_if<Engine::StreamingTerrainChunkPayload>(&payload);
                if (!terrain) {
                    result.status = Engine::StreamingPromotionStatus::CallbackFailed;
                    result.message = "Terrain chunk payload type mismatch.";
                    return result;
                }
                Engine::TerrainImportedChunk chunk;
                chunk.id = terrain->chunkId;
                chunk.coord = terrain->chunkId.coord;
                chunk.origin = terrain->origin;
                chunk.size = terrain->size;
                chunk.resolution = terrain->resolution;
                chunk.heights = terrain->heights;
                chunk.warnings = terrain->warnings;
                Engine::TerrainChunkHandle handle = runtime.terrain.loadImportedChunk(runtime.terrainSource, chunk);
                if (!Engine::isValid(handle)) {
                    result.status = Engine::StreamingPromotionStatus::CallbackFailed;
                    result.message = "Failed to load streamed terrain chunk into TerrainDataset.";
                    return result;
                }
                runtime.streamingTerrainChunks[token] = handle;
                result.liveResources.sceneComponents = static_cast<uint32_t>(runtime.streamingTerrainChunks.size());
                result.status = Engine::StreamingPromotionStatus::Success;
                result.message = "Promoted terrain CPU chunk.";
                return result;
            }

            if (request.payload == Engine::StreamingPayloadKind::TerrainRenderLod) {
                const Engine::StreamingReadDescriptorEntry* descriptor = modernStreamingDescriptor(runtime, request);
                if (!descriptor || !descriptor->descriptor.terrainChunkManifest) {
                    result.status = Engine::StreamingPromotionStatus::MissingCachedPayload;
                    result.message = "Missing terrain LOD descriptor for promotion.";
                    return result;
                }
                const Engine::TerrainDerivedCacheLodMeshReadResult read =
                    Engine::TerrainDerivedCache::readLodMesh(*descriptor->descriptor.terrainChunkManifest);
                if (read.status != Engine::TerrainDerivedCacheStatus::Hit || !read.payload) {
                    result.status = Engine::StreamingPromotionStatus::MissingCachedPayload;
                    result.message = "Terrain LOD payload was not readable during promotion.";
                    return result;
                }
                std::vector<Renderer::MeshVertex> vertices;
                vertices.reserve(read.payload->vertices.size());
                for (const Engine::TerrainCpuMeshVertex& vertex : read.payload->vertices) {
                    vertices.push_back(meshVertexFromTerrainCpu(vertex));
                }
                Renderer::TerrainHandle terrain = Renderer::createTerrainTile(
                    vertices,
                    read.payload->indices,
                    runtime.terrainFallbackMaterial);
                Renderer::setTerrainRenderLayer(terrain, Renderer::RenderLayer::Terrain);
                Renderer::setTerrainMaxDrawDistance(terrain, 500.0f);
                if (runtime.terrainMaterial.materialSet.id != UINT32_MAX) {
                    Renderer::setTerrainMaterialSet(terrain, runtime.terrainMaterial.materialSet);
                }
                runtime.streamingTerrainHandles[token] = terrain;
                ++runtime.terrainRendererCount;
                result.liveResources.terrainRenderHandles =
                    static_cast<uint32_t>(runtime.streamingTerrainHandles.size());
                result.status = Engine::StreamingPromotionStatus::Success;
                result.message = "Promoted terrain render LOD.";
                return result;
            }

            if (request.payload == Engine::StreamingPayloadKind::NavigationTile) {
                const auto* tile = std::get_if<Engine::StreamingNavigationTilePayload>(&payload);
                if (!tile) {
                    result.status = Engine::StreamingPromotionStatus::CallbackFailed;
                    result.message = "Navigation tile payload type mismatch.";
                    return result;
                }
                Engine::NavigationTileCacheData cacheData;
                cacheData.coord = tile->coord;
                cacheData.bounds = {tile->bounds.min, tile->bounds.max};
                cacheData.detourTileData = tile->detourTileData;
                Engine::NavigationTileHandle handle = runtime.navigation.loadTerrainTileFromCache(cacheData);
                if (handle.id == UINT32_MAX) {
                    result.status = Engine::StreamingPromotionStatus::CallbackFailed;
                    result.message = "Failed to insert streamed navigation tile.";
                    return result;
                }
                runtime.streamingNavigationTiles[token] = handle;
                const std::vector<Engine::ChunkCoord> dirtyConnectivity =
                    chunkAndCardinalNeighbors(cacheData.coord);
                runtime.navigationConnectivity.rebuildChunks(
                    dirtyConnectivity,
                    runtime.navigation,
                    Engine::NavAgentSettings{});
                const Engine::NavigationConnectivityStats connectivityStats = runtime.navigationConnectivity.stats();
                runtime.navigationStatus = "Updated streamed navigation connectivity: chunks " +
                    std::to_string(connectivityStats.chunkCount) +
                    ", portals " + std::to_string(connectivityStats.totalPortals) +
                    ", connected " + std::to_string(connectivityStats.connectedPortals) + ".";
                ++runtime.terrainNavTileCount;
                result.liveResources.navigationTiles =
                    static_cast<uint32_t>(runtime.streamingNavigationTiles.size());
                result.status = Engine::StreamingPromotionStatus::Success;
                result.message = "Promoted navigation tile.";
                return result;
            }

            if (request.payload == Engine::StreamingPayloadKind::PhysicsCollider) {
                const Engine::StreamingReadDescriptorEntry* descriptor = modernStreamingDescriptor(runtime, request);
                if (!descriptor || !descriptor->descriptor.terrainChunkManifest) {
                    result.status = Engine::StreamingPromotionStatus::MissingCachedPayload;
                    result.message = "Missing terrain physics descriptor for promotion.";
                    return result;
                }
                const Engine::TerrainDerivedCachePhysicsColliderReadResult read =
                    Engine::TerrainDerivedCache::readPhysicsCollider(*descriptor->descriptor.terrainChunkManifest);
                if (read.status != Engine::TerrainDerivedCacheStatus::Hit || !read.payload) {
                    result.status = Engine::StreamingPromotionStatus::MissingCachedPayload;
                    result.message = "Terrain physics collider payload was not readable during promotion.";
                    return result;
                }
                Engine::TerrainPhysicsColliderCreateDescriptor create;
                create.layer = {4u};
                create.debugName = "streamed terrain physics";
                Engine::TerrainPhysicsColliderHandle handle =
                    runtime.terrainPhysics.createStaticCollider(runtime.scene, runtime.physics, *read.payload, create);
                if (!Engine::isValid(handle)) {
                    result.status = Engine::StreamingPromotionStatus::CallbackFailed;
                    result.message = "Failed to create streamed terrain physics collider.";
                    return result;
                }
                runtime.streamingPhysicsColliders[token] = handle;
                ++runtime.terrainPhysicsColliderCount;
                result.liveResources.physicsColliders =
                    static_cast<uint32_t>(runtime.streamingPhysicsColliders.size());
                result.status = Engine::StreamingPromotionStatus::Success;
                result.message = "Promoted terrain physics collider.";
                return result;
            }

            if (request.payload == Engine::StreamingPayloadKind::AssetDependency) {
                result.status = Engine::StreamingPromotionStatus::Success;
                result.liveResources.assetDependencies = 1;
                result.message = "Promoted metadata-only asset dependency.";
                return result;
            }

            return result;
        };

        callbacks.demote = [&runtime](
            const Engine::StreamingDemotionRequest& request,
            Engine::StreamingRuntimeToken token) {
            Engine::StreamingPromotionResult result;
            result.status = Engine::StreamingPromotionStatus::Success;
            if (request.payload == Engine::StreamingPayloadKind::TerrainChunk) {
                if (auto it = runtime.streamingTerrainChunks.find(token.value);
                    it != runtime.streamingTerrainChunks.end()) {
                    runtime.terrain.unloadChunk(it->second);
                    runtime.streamingTerrainChunks.erase(it);
                }
                return result;
            }
            if (request.payload == Engine::StreamingPayloadKind::TerrainRenderLod) {
                if (auto it = runtime.streamingTerrainHandles.find(token.value);
                    it != runtime.streamingTerrainHandles.end()) {
                    Renderer::destroyTerrainTile(it->second);
                    runtime.streamingTerrainHandles.erase(it);
                    runtime.terrainRendererCount =
                        static_cast<uint32_t>(runtime.streamingTerrainHandles.size());
                }
                return result;
            }
            if (request.payload == Engine::StreamingPayloadKind::NavigationTile) {
                if (auto it = runtime.streamingNavigationTiles.find(token.value);
                    it != runtime.streamingNavigationTiles.end()) {
                    runtime.navigation.destroyTile({
                        request.key.terrainChunk.coord.x,
                        request.key.terrainChunk.coord.z,
                    });
                    runtime.navigationConnectivity.removeChunk({
                        request.key.terrainChunk.coord.x,
                        request.key.terrainChunk.coord.z,
                    });
                    runtime.navigationConnectivity.relinkChunkAndNeighbors({
                        request.key.terrainChunk.coord.x,
                        request.key.terrainChunk.coord.z,
                    });
                    runtime.streamingNavigationTiles.erase(it);
                    runtime.terrainNavTileCount =
                        static_cast<uint32_t>(runtime.streamingNavigationTiles.size());
                }
                return result;
            }
            if (request.payload == Engine::StreamingPayloadKind::PhysicsCollider) {
                if (auto it = runtime.streamingPhysicsColliders.find(token.value);
                    it != runtime.streamingPhysicsColliders.end()) {
                    runtime.terrainPhysics.destroyCollider(runtime.scene, runtime.physics, it->second);
                    runtime.streamingPhysicsColliders.erase(it);
                    runtime.terrainPhysicsColliderCount =
                        static_cast<uint32_t>(runtime.streamingPhysicsColliders.size());
                }
                return result;
            }
            return result;
        };
        return callbacks;
    }

    bool addModernTerrainChunk(
        ModernDefaultSceneRuntime& runtime,
        Engine::TerrainChunkHandle chunk,
        const Engine::TerrainSourceDescriptor& source,
        Renderer::MaterialHandle terrainMaterial)
    {
        const std::optional<Engine::TerrainChunkData> data = runtime.terrain.chunk(chunk);
        if (!data) {
            return false;
        }

        Engine::TerrainLodMeshBuildSettings lod;
        lod.lodIndex = 0;
        lod.renderResolution = std::min<uint32_t>(data->resolution, 33);
        lod.skirtDepth = 2.0f;
        const std::optional<Engine::TerrainRenderLodBuildRequest> renderRequest =
            Engine::renderLodRequestFromDatasetChunk(
                runtime.terrain,
                chunk,
                lod,
                modernTerrainRenderIdentity(source));
        if (!renderRequest) {
            ++runtime.warningCount;
            return false;
        }
        const Engine::TerrainRenderLodBuildResult renderResult = Engine::buildTerrainRenderLod(*renderRequest);
        if (!renderResult.success || !renderResult.build.success) {
            SDL_Log("Modern terrain render LOD failed for chunk %d,%d: %s",
                data->coord.x,
                data->coord.z,
                renderResult.diagnostics.message.c_str());
            ++runtime.warningCount;
            return false;
        }

        Renderer::TerrainHandle rendererTerrain = Renderer::createTerrainTile(
            renderResult.build.mesh.vertices,
            renderResult.build.mesh.indices,
            terrainMaterial);
        Renderer::setTerrainRenderLayer(rendererTerrain, Renderer::RenderLayer::Terrain);
        Renderer::setTerrainMaxDrawDistance(rendererTerrain, 500.0f);
        if (runtime.terrainMaterial.materialSet.id != UINT32_MAX) {
            Renderer::setTerrainMaterialSet(rendererTerrain, runtime.terrainMaterial.materialSet);
        }

        Engine::NavigationTileHandle navTile;

        const std::optional<Engine::TerrainPhysicsColliderBuildRequest> physicsRequest =
            Engine::terrainPhysicsColliderRequestFromDatasetChunk(
                runtime.terrain,
                chunk,
                17,
                modernTerrainPhysicsIdentity(source));
        Engine::TerrainPhysicsColliderHandle physicsCollider;
        if (physicsRequest) {
            Engine::TerrainPhysicsColliderBuildResult physicsData =
                Engine::buildTerrainPhysicsCollider(*physicsRequest);
            if (physicsData.success && physicsData.payload) {
                Engine::TerrainPhysicsColliderCreateDescriptor descriptor;
                descriptor.layer = {4u};
                descriptor.debugName = "modern terrain " +
                    std::to_string(data->coord.x) + "," + std::to_string(data->coord.z);
                physicsCollider = runtime.terrainPhysics.createStaticCollider(
                    runtime.scene,
                    runtime.physics,
                    *physicsData.payload,
                    descriptor);
                if (Engine::isValid(physicsCollider)) {
                    ++runtime.terrainPhysicsColliderCount;
                }
            } else {
                ++runtime.warningCount;
            }
        }

        if (const std::optional<Engine::TerrainDatasetBounds> bounds = runtime.terrain.chunkWorldBounds(chunk)) {
            includeBounds(runtime.bounds, {bounds->min, bounds->max});
        }
        ++runtime.terrainRendererCount;
        runtime.terrainChunks.push_back({
            chunk,
            {data->coord.x, data->coord.z},
            rendererTerrain,
            navTile,
            physicsCollider,
        });
        return true;
    }

    bool loadModernHeightmapTerrain(
        ModernDefaultSceneRuntime& runtime,
        Engine::AssetCache& assetCache)
    {
        runtime.terrainFallbackTexture = assetCache.acquireSolidTexture(82, 124, 72, 255);
        runtime.terrainFallbackMaterial =
            createMaterial("modern.terrain.fallback", runtime.terrainFallbackTexture.handle);

        const std::array<Engine::TerrainSampleBiomeMaterialInput, 1> terrainMaterials{{
            {"heightmap", {82, 124, 72, 255}},
        }};
        const Engine::TerrainMaterialSet materialSet =
            Engine::makeSampleProceduralTerrainMaterialSet(terrainMaterials, {82, 124, 72, 255});
        Engine::TerrainMaterialRenderCreateResult materialResult =
            Engine::createTerrainMaterialResources(assetCache, materialSet);
        if (materialResult.success) {
            runtime.terrainMaterial = std::move(materialResult.binding);
        } else {
            ++runtime.warningCount;
        }

        Engine::TerrainSourceDescriptor source;
        std::vector<Engine::TerrainImportedChunk> chunksToLoad;
        const std::filesystem::path heightmapPath = resolveAuthoredScenePath(DefaultHeightmapPath);
        if (std::filesystem::exists(heightmapPath)) {
            Engine::TerrainHeightmapImportSettings settings;
            settings.sourcePath = heightmapPath;
            settings.sourceIdOverride = modernAssetIdForPath(heightmapPath, "modern_heightmap");
            settings.sampleSpacing = 1.0f;
            settings.heightScale = 80.0f;
            settings.heightOffset = 0.0f;
            settings.sourceOrigin = {-256.0f, 0.0f, 256.0f};
            settings.chunkWorldSize = 64.0f;
            settings.chunkResolution = 33;
            Engine::TerrainHeightmapTerrainImportResult imported = Engine::importHeightmapTerrain(settings);
            if (imported.success) {
                source.sourceId = imported.metadata.sourceId;
                source.type = Engine::TerrainDatasetSourceType::HeightmapImported;
                source.bounds = {imported.metadata.worldMin, imported.metadata.worldMax};
                source.defaultChunkSize = settings.chunkWorldSize;
                source.defaultResolution = settings.chunkResolution;
                source.settings = imported.metadata.importSettings;
                source.debugName = "modern.default.heightmap";

                int32_t maxX = 0;
                int32_t maxZ = 0;
                for (const Engine::TerrainImportedChunk& chunk : imported.chunks) {
                    maxX = std::max(maxX, chunk.coord.x);
                    maxZ = std::max(maxZ, chunk.coord.z);
                }
                const int32_t centerX = maxX / 2;
                const int32_t centerZ = maxZ / 2;
                for (const Engine::TerrainImportedChunk& chunk : imported.chunks) {
                    if (std::abs(chunk.coord.x - centerX) <= 1 &&
                        std::abs(chunk.coord.z - centerZ) <= 1) {
                        chunksToLoad.push_back(chunk);
                    }
                }
                runtime.loadedHeightmap = !chunksToLoad.empty();
                runtime.warningCount += static_cast<uint32_t>(imported.warnings.size());
            } else {
                SDL_Log("Modern heightmap terrain import failed: %s", imported.message.c_str());
                ++runtime.warningCount;
            }
        }

        if (!runtime.loadedHeightmap) {
            source = modernFallbackTerrainSourceDescriptor();
        }

        runtime.terrainSource = runtime.terrain.registerSource(source);
        if (!Engine::isValid(runtime.terrainSource)) {
            runtime.status = "Failed to register modern terrain source.";
            return false;
        }

        if (runtime.loadedHeightmap) {
            for (const Engine::TerrainImportedChunk& chunk : chunksToLoad) {
                const Engine::TerrainChunkHandle handle =
                    runtime.terrain.loadImportedChunk(runtime.terrainSource, chunk);
                if (Engine::isValid(handle)) {
                    addModernTerrainChunk(runtime, handle, source, runtime.terrainFallbackMaterial);
                }
            }
        } else {
            for (int32_t z = -1; z <= 1; ++z) {
                for (int32_t x = -1; x <= 1; ++x) {
                    const Engine::TerrainChunkHandle handle =
                        runtime.terrain.loadProceduralChunk(runtime.terrainSource, {x, z});
                    if (Engine::isValid(handle)) {
                        addModernTerrainChunk(runtime, handle, source, runtime.terrainFallbackMaterial);
                    }
                }
            }
        }

        return !runtime.terrainChunks.empty();
    }

    bool initializeModernStreamingTerrain(
        ModernDefaultSceneRuntime& runtime,
        Engine::AssetCache& assetCache,
        const ManualEngine::App::EditorProjectSettings* editorSettings = nullptr)
    {
        runtime.terrainFallbackTexture = assetCache.acquireSolidTexture(82, 124, 72, 255);
        runtime.terrainFallbackMaterial =
            createMaterial("modern.terrain.streaming.fallback", runtime.terrainFallbackTexture.handle);

        const std::array<Engine::TerrainSampleBiomeMaterialInput, 1> terrainMaterials{{
            {"heightmap", {82, 124, 72, 255}},
        }};
        const Engine::TerrainMaterialSet materialSet =
            Engine::makeSampleProceduralTerrainMaterialSet(terrainMaterials, {82, 124, 72, 255});
        Engine::TerrainMaterialRenderCreateResult materialResult =
            Engine::createTerrainMaterialResources(assetCache, materialSet);
        if (materialResult.success) {
            runtime.terrainMaterial = std::move(materialResult.binding);
        } else {
            ++runtime.warningCount;
        }

        Engine::OpenWorldStreamingRuntimeSettings settings =
            modernStreamingRuntimeSettings({}, editorSettings);
        SDL_Log("Modern streaming build validation started: %s",
            settings.savedBuildManifestPath.generic_string().c_str());
        runtime.streamingRuntime = Engine::OpenWorldStreamingRuntime{settings};
        const Engine::OpenWorldStreamingBuildResult& build = runtime.streamingRuntime.initializeFromSavedBuild();
        if (!build.success) {
            SDL_Log("Modern streaming build validation failed: %s", build.message.c_str());
            ++runtime.warningCount;
            return false;
        }
        if (build.rebuilt) {
            SDL_Log("Modern streaming build rebuilt: %s (%u chunks, %u payload writes).",
                build.message.c_str(),
                build.bakeDiagnostics.importedChunkCount,
                build.bakeDiagnostics.terrainChunkWrites +
                    build.bakeDiagnostics.renderLodWrites +
                    build.bakeDiagnostics.navigationTileWrites +
                    build.bakeDiagnostics.physicsColliderWrites);
        } else {
            SDL_Log("Modern streaming build reused: %s", build.message.c_str());
        }

        Engine::TerrainSourceDescriptor source = modernStreamingTerrainSourceDescriptor(settings);
        const Renderer::Aabb manifestBounds = boundsFromStreamingManifest(runtime.streamingRuntime.manifest());
        source.bounds = {manifestBounds.min, manifestBounds.max};
        runtime.bounds = manifestBounds;
        runtime.terrainSource = runtime.terrain.registerSource(source);
        if (!Engine::isValid(runtime.terrainSource)) {
            SDL_Log("Modern streaming terrain source registration failed.");
            ++runtime.warningCount;
            return false;
        }
        runtime.loadedHeightmap = true;
        SDL_Log("Modern runtime streaming active.");
        return true;
    }

    void rebuildModernNavigationWithSceneGeometry(
        ModernDefaultSceneRuntime& runtime,
        const Engine::TerrainSourceDescriptor& source)
    {
        runtime.terrainNavTileCount = 0;
        runtime.navigationCacheHitCount = 0;
        runtime.navigationCacheMissCount = 0;
        runtime.navigationCacheStaleCount = 0;
        runtime.navigationCacheWriteCount = 0;

        Engine::NavAgentSettings agent;
        Engine::SceneNavigationGeometryBuildSettings geometrySettings;
        geometrySettings.maxWalkableSlopeDegrees = agent.maxSlopeDegrees;
        const float navigationBorderPadding = std::max(
            agent.radius * 2.0f,
            runtime.navigation.settings().cellSize * 4.0f);
        const uint32_t navigationBorderSamples = static_cast<uint32_t>(std::ceil(
            navigationBorderPadding / std::max(source.defaultChunkSize / 16.0f, 0.0001f)));
        geometrySettings.tileBoundsPadding = agent.radius + navigationBorderPadding;

        Engine::NavigationCacheSettings cacheSettings;
        cacheSettings.worldId = "modern_default_scene";
        const Engine::TerrainNavigationSourceIdentity terrainIdentity = modernTerrainNavigationIdentity(source);
        Engine::NavigationCacheManifest cacheManifest = Engine::NavigationCache::buildManifest(
            cacheSettings,
            source.defaultChunkSize,
            0,
            17,
            runtime.navigation.settings(),
            agent,
            "modern_default",
            {},
            {},
            terrainIdentity.sourceId,
            terrainIdentity.sourceHash,
            terrainIdentity.importSettings,
            modernTerrainSourceTypeName(terrainIdentity.sourceType),
            Engine::TerrainNavigationAdapterVersion,
            navigationBorderPadding,
            navigationBorderSamples,
            modernSceneGeometryHash(runtime),
            geometrySettings.maxWalkableSlopeDegrees,
            geometrySettings.tileBoundsPadding,
            "modern_scene_nav_geometry_slope_v1");
        Engine::NavigationCache cache(cacheSettings, cacheManifest);
        cache.ensureManifest();

        for (ModernTerrainChunkRuntime& chunkRuntime : runtime.terrainChunks) {
            runtime.navigation.destroyTile(chunkRuntime.coord);
            chunkRuntime.navigationTile = {};

            const Engine::NavigationCacheTileReadResult cached =
                Engine::NavigationCache::readTileCache(cacheSettings, cacheManifest, chunkRuntime.coord);
            if (cached.status == Engine::NavigationCacheOperationStatus::Hit && cached.tile) {
                chunkRuntime.navigationTile = runtime.navigation.loadTerrainTileFromCache(*cached.tile);
                if (chunkRuntime.navigationTile.id != UINT32_MAX) {
                    ++runtime.terrainNavTileCount;
                    ++runtime.navigationCacheHitCount;
                    continue;
                }
            }
            if (cached.status == Engine::NavigationCacheOperationStatus::Stale ||
                cached.status == Engine::NavigationCacheOperationStatus::Corrupt) {
                ++runtime.navigationCacheStaleCount;
            } else {
                ++runtime.navigationCacheMissCount;
            }

            Engine::TerrainNavigationBuildSettings navSettings;
            navSettings.navigationResolution = 17;
            navSettings.borderPaddingWorld = navigationBorderPadding;
            navSettings.borderSampleCount = navigationBorderSamples;
            const std::optional<Engine::TerrainNavigationBuildRequest> navRequest =
                Engine::terrainNavigationRequestFromDatasetNeighborhood(
                    runtime.terrain,
                    runtime.terrainSource,
                    {chunkRuntime.coord.x, chunkRuntime.coord.z},
                    navSettings,
                    terrainIdentity);
            if (!navRequest) {
                ++runtime.warningCount;
                continue;
            }

            Engine::TerrainNavigationBuildResult terrainNav = Engine::buildTerrainNavigationData(*navRequest);
            if (!terrainNav.success || !terrainNav.buildData) {
                ++runtime.warningCount;
                continue;
            }

            Engine::SceneNavigationBuildRequest sceneRequest;
            sceneRequest.coord = chunkRuntime.coord;
            sceneRequest.bounds = terrainNav.buildData->bounds;
            sceneRequest.captureDebug = BuildDebugToolsEnabled;
            const std::optional<Engine::NavigationTerrainBuildData> combined =
                runtime.sceneNavigationGeometry.buildNavigationData(
                    runtime.scene,
                    sceneRequest,
                    &*terrainNav.buildData,
                    geometrySettings);
            if (!combined) {
                ++runtime.warningCount;
                continue;
            }

            const Engine::NavigationTileBuildResult built =
                Engine::NavigationSystem::buildTerrainTileData(*combined, agent, runtime.navigation.settings());
            if (built.status == Engine::NavQueryStatus::Success && built.tileData) {
                chunkRuntime.navigationTile = runtime.navigation.loadTerrainTileFromCache(*built.tileData, built.diagnostics);
                ++runtime.terrainNavTileCount;
                const Engine::NavigationCacheWriteResult write =
                    Engine::NavigationCache::writeTileCache(cacheSettings, cacheManifest, *built.tileData);
                if (write.status == Engine::NavigationCacheOperationStatus::WriteSuccess) {
                    ++runtime.navigationCacheWriteCount;
                }
            } else {
                ++runtime.warningCount;
            }
        }
        runtime.navigationStatus =
            rebuildModernNavigationConnectivityFromLoadedTiles(runtime.navigationConnectivity, runtime.navigation);
        SDL_Log("%s", runtime.navigationStatus.c_str());
    }

    std::unique_ptr<ModernDefaultSceneRuntime> startModernDefaultSceneRuntime(
        Engine::AssetCache& assetCache,
        const ManualEngine::App::EditorProjectSettings* editorSettings = nullptr)
    {
        auto runtime = std::make_unique<ModernDefaultSceneRuntime>();
        runtime->characters.setNavigationService(&runtime->navigationService);
        Engine::NavigationConnectivitySettings connectivitySettings = runtime->navigationConnectivity.settings();
        connectivitySettings.requireCenterReachability = false;
        runtime->navigationConnectivity.setSettings(connectivitySettings);
        runtime->bounds = {{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()},
            {-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()}};

        runtime->usingOpenWorldStreaming = initializeModernStreamingTerrain(*runtime, assetCache, editorSettings);
        if (!runtime->usingOpenWorldStreaming) {
            (void)loadModernHeightmapTerrain(*runtime, assetCache);
        }
        const std::optional<Engine::TerrainSourceDescriptor> terrainSource =
            runtime->terrain.sourceMetadata(runtime->terrainSource);

        glm::vec3 focus{
            (runtime->bounds.min.x + runtime->bounds.max.x) * 0.5f,
            0.0f,
            (runtime->bounds.min.z + runtime->bounds.max.z) * 0.5f,
        };
        focus.y = runtime->terrain.sampleHeight(focus.x, focus.z).value_or(0.0f);
        runtime->focus = focus;

        (void)addModernAuthoredAsset(
            *runtime,
            assetCache,
            DefaultSponzaScenePath,
            "sponza_gltf",
            modernTerrainRelativePosition(*runtime, focus, {0.0f, 2.0f, 0.0f}),
            0.25f);
        (void)addModernAuthoredAsset(
            *runtime,
            assetCache,
            DefaultKayKitStaticFbxPath,
            "kaykit_static_fbx",
            modernTerrainRelativePosition(*runtime, focus, {-18.0f, 4.0f, -18.0f}),
            2.5f);
        (void)addModernAuthoredAsset(
            *runtime,
            assetCache,
            DefaultKayKitStaticGltfPath,
            "kaykit_static_gltf",
            modernTerrainRelativePosition(*runtime, focus, {18.0f, 4.0f, -18.0f}),
            2.5f);
        (void)addModernAnimatedAsset(
            *runtime,
            assetCache,
            ReleaseKayKitAnimatedGltfPath,
            "kaykit_skinned_gltf",
            modernTerrainRelativePosition(*runtime, focus, {-22.0f, 6.0f, 24.0f}),
            3.0f);
        (void)addModernAnimatedAsset(
            *runtime,
            assetCache,
            DefaultKayKitAnimatedFbxPath,
            "kaykit_skinned_fbx",
            modernTerrainRelativePosition(*runtime, focus, {22.0f, 6.0f, 24.0f}),
            3.0f);

        runtime->scene.updateWorldTransforms();
        if (terrainSource && !runtime->usingOpenWorldStreaming) {
            rebuildModernNavigationWithSceneGeometry(*runtime, *terrainSource);
        }

        if (!Engine::isValid(runtime->playerActor)) {
            runtime->playerActor = runtime->scene.createActor({0x4d4f4445524e01ull});
            Engine::SceneTransform playerTransform;
            playerTransform.translation = modernTerrainRelativePosition(*runtime, focus, {0.0f, 24.0f, -30.0f});
            runtime->scene.setLocalTransform(runtime->playerActor, playerTransform);
        } else {
            if (std::optional<Engine::SceneTransform> playerTransform =
                    runtime->scene.localTransform(runtime->playerActor)) {
                playerTransform->translation = modernTerrainRelativePosition(*runtime, focus, {0.0f, 8.0f, -30.0f});
                runtime->scene.setLocalTransform(runtime->playerActor, *playerTransform);
            }
        }
        runtime->playerCharacter = createModernPlayerCharacter(*runtime);
        if (!Engine::isValid(runtime->playerCharacter)) {
            ++runtime->warningCount;
        }

        if (!Engine::isValid(runtime->physics.registerPhysicsSystems())) {
            ++runtime->warningCount;
        }
        if (!Engine::isValid(runtime->characters.registerMovementSystem())) {
            ++runtime->warningCount;
        }
        if (!Engine::isValid(runtime->animatedAdapter.registerVariableAnimationSystem())) {
            ++runtime->warningCount;
        }
        if (!Engine::isValid(runtime->renderBridge.registerPreRenderSystem(runtime->renderBackend))) {
            ++runtime->warningCount;
        }
        runtime->scene.load();
        runtime->scene.start();
        runtime->schedulerStarted = runtime->scene.lifecycleState() == Engine::SceneLifecycleState::Started;
        runtime->status = "Modern default scene: terrain " +
            std::string{runtime->loadedHeightmap ? "heightmap" : "procedural fallback"} +
            std::string{runtime->usingOpenWorldStreaming ? " streaming" : " eager"} +
            ", terrain chunks " + std::to_string(runtime->terrainChunks.size()) +
            ", authored nav sources " + std::to_string(runtime->authoredNavigationSourceCount) +
            ", authored physics bodies " + std::to_string(runtime->authoredPhysicsBodyCount) +
            ", static assets " + std::to_string(runtime->staticAssetCount) +
            ", animated assets " + std::to_string(runtime->animatedAssetCount) +
            ", warnings " + std::to_string(runtime->warningCount);
        SDL_Log("%s", runtime->status.c_str());
        return runtime;
    }

}
