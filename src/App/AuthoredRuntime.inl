    enum class AppSceneMode {
        ModernDefault,
        LegacyProcedural,
        LegacyAuthored,
    };

    constexpr const char* DefaultSponzaScenePath = "assets/main_sponza/NewSponza_Main_glTF_003.gltf";
    constexpr const char* DemoAuthoredSceneFixturePath = "tests/assets/fixtures/authored_scene_fixture.gltf";
    constexpr const char* DemoAnimatedModelFixturePath = "tests/assets/fixtures/skinned_animation_fixture.gltf";
    constexpr const char* DefaultHeightmapPath = "assets/heightmaps/47_648_-122_332_13_505_505_16bit.png";
    constexpr const char* DefaultKayKitStaticFbxPath =
        "assets/KayKit_Adventurers_2.0_FREE/Assets/fbx/sword_1handed.fbx";
    constexpr const char* DefaultKayKitStaticGltfPath =
        "assets/KayKit_Adventurers_2.0_FREE/Assets/gltf/shield_round.gltf";
    constexpr const char* ReleaseKayKitAnimatedModelPath =
        "assets/KayKit_Adventurers_2.0_FREE/Animations/gltf/Rig_Medium/Rig_Medium_MovementBasic.glb";
    constexpr const char* DefaultKayKitAnimatedFbxPath =
        "assets/KayKit_Adventurers_2.0_FREE/Animations/fbx/Rig_Medium/Rig_Medium_MovementBasic.fbx";
    constexpr const char* ReleaseKayKitKnightTexturePath =
        "assets/KayKit_Adventurers_2.0_FREE/Characters/gltf/knight_texture.png";

    struct AppSceneSelection {
        AppSceneMode mode = AppSceneMode::ModernDefault;
        std::filesystem::path authoredPath = DefaultSponzaScenePath;
        std::filesystem::path animatedModelPath = ReleaseKayKitAnimatedModelPath;
        bool authoredPathExplicit = false;
        bool animatedModelPathExplicit = false;
    };

    std::string_view sceneModeName(AppSceneMode mode)
    {
        switch (mode) {
            case AppSceneMode::ModernDefault:
                return "ModernDefault";
            case AppSceneMode::LegacyProcedural:
                return "LegacyProcedural";
            case AppSceneMode::LegacyAuthored:
                return "LegacyAuthored";
        }
        return "Unknown";
    }

    std::vector<Engine::ChunkCoord> chunkAndCardinalNeighbors(Engine::ChunkCoord coord)
    {
        return {
            coord,
            {coord.x + 1, coord.z},
            {coord.x - 1, coord.z},
            {coord.x, coord.z + 1},
            {coord.x, coord.z - 1},
        };
    }

    std::vector<Engine::ChunkCoord> loadedNavigationTileCoords(const Engine::NavigationSystem& navigation)
    {
        std::vector<Engine::ChunkCoord> coords;
        for (const Engine::NavigationTileDiagnostics& diagnostics : navigation.allTileDiagnostics()) {
            coords.push_back(diagnostics.coord);
        }
        std::ranges::sort(coords, [](Engine::ChunkCoord lhs, Engine::ChunkCoord rhs) {
            return lhs.x == rhs.x ? lhs.z < rhs.z : lhs.x < rhs.x;
        });
        coords.erase(std::unique(coords.begin(), coords.end()), coords.end());
        return coords;
    }

    std::string rebuildModernNavigationConnectivityFromLoadedTiles(
        Engine::NavigationConnectivitySystem& connectivity,
        const Engine::NavigationSystem& navigation)
    {
        const std::vector<Engine::ChunkCoord> loaded = loadedNavigationTileCoords(navigation);
        if (loaded.empty()) {
            connectivity.clear();
            return "No live navigation tiles are loaded; connectivity was cleared.";
        }
        connectivity.rebuild(loaded, navigation, Engine::NavAgentSettings{});
        const Engine::NavigationConnectivityStats stats = connectivity.stats();
        return "Rebuilt navigation connectivity from " + std::to_string(loaded.size()) +
            " live tiles: chunks " + std::to_string(stats.chunkCount) +
            ", portals " + std::to_string(stats.totalPortals) +
            ", connected " + std::to_string(stats.connectedPortals) + ".";
    }

    AppSceneSelection parseSceneSelection(int argc, char** argv)
    {
        AppSceneSelection selection;
        for (int index = 1; index < argc; ++index) {
            const std::string_view argument = argv[index] ? std::string_view{argv[index]} : std::string_view{};
            if (argument == "--scene" && index + 1 < argc) {
                const std::string_view value = argv[++index] ? std::string_view{argv[index]} : std::string_view{};
                if (value == "modern" || value == "default" || value == "modern-default") {
                    selection.mode = AppSceneMode::ModernDefault;
                } else if (value == "legacy-authored" || value == "authored") {
                    selection.mode = AppSceneMode::LegacyAuthored;
                } else if (value == "legacy-procedural" || value == "procedural") {
                    selection.mode = AppSceneMode::LegacyProcedural;
                } else {
                    SDL_Log("Invalid --scene value '%.*s'; using %s.",
                        static_cast<int>(value.size()),
                        value.data(),
                        sceneModeName(selection.mode).data());
                }
            } else if (argument == "--scene-path" && index + 1 < argc) {
                selection.authoredPath = argv[++index];
                selection.authoredPathExplicit = true;
            } else if (argument == "--animated-model-path" && index + 1 < argc) {
                selection.animatedModelPath = argv[++index];
                selection.animatedModelPathExplicit = true;
            } else if (argument == "--scene" || argument == "--scene-path" || argument == "--animated-model-path") {
                SDL_Log("Missing value for %.*s; using default scene selection.",
                    static_cast<int>(argument.size()),
                    argument.data());
            }
        }
        return selection;
    }

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

    std::filesystem::path chooseStartupAuthoredScenePath(const AppSceneSelection& selection)
    {
        if (selection.authoredPathExplicit) {
            return selection.authoredPath;
        }

        const std::filesystem::path resolvedDefault = resolveAuthoredScenePath(selection.authoredPath);
        if (std::filesystem::exists(resolvedDefault)) {
            return selection.authoredPath;
        }

        const std::filesystem::path resolvedFixture = resolveAuthoredScenePath(DemoAuthoredSceneFixturePath);
        if (std::filesystem::exists(resolvedFixture)) {
            SDL_Log(
                "Default Sponza scene is unavailable at %s; using committed authored fixture demo.",
                resolvedDefault.generic_string().c_str());
            return DemoAuthoredSceneFixturePath;
        }

        return selection.authoredPath;
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

    struct AuthoredRuntime {
        Engine::AuthoredScene scene;
        Engine::PartitionedAuthoredScene streamingScene;
        Engine::Scene sceneRuntime;
        std::unique_ptr<Engine::SceneRenderBridge> sceneRenderBridge;
        std::unique_ptr<Engine::SceneAnimatedModelAdapter> sceneAnimatedAdapter;
        Engine::RendererSceneRenderBackend sceneRenderBackend;
        Engine::SceneAuthoredAdapterResult sceneAdapter;
        Engine::SceneAnimatedAdapterResult sceneAnimatedResult;
        AuthoredFallbackScene fallback;
        bool usingFallback = false;
        bool usingStreaming = false;
        bool usingSceneAdapter = false;
        bool usingSceneAnimatedAdapter = false;
        bool sceneSchedulerStarted = false;
        bool sceneAnimatedPlaced = false;
        glm::vec3 sceneAnimatedFocus{0.0f};
        float sceneAnimatedCameraDistance = 20.0f;
        bool showingPlaceholder = false;
        bool frameSceneAfterCommit = false;
        enum class AsyncPhase {
            Idle,
            CacheReadPending,
            ImportPending,
            CacheWritePending,
            Committed,
            Failed,
        } asyncPhase = AsyncPhase::Idle;
        std::filesystem::path sourcePath;
        Engine::AuthoredSceneStreamingSettings settings;
        Engine::AuthoredSceneCacheManifest cacheManifest;
        Engine::AsyncJobHandle cacheReadJob;
        Engine::AsyncJobHandle importJob;
        Engine::AsyncJobHandle cacheWriteJob;
        uint32_t asyncJobsQueued = 0;
        uint32_t asyncJobsCompleted = 0;
        uint32_t asyncJobsFailed = 0;
        float cacheReadMs = 0.0f;
        float importMs = 0.0f;
        float cacheWriteMs = 0.0f;
        std::string status;
        Renderer::Aabb bounds{{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}};

        bool startSceneScheduler()
        {
            if (!sceneRenderBridge) {
                return false;
            }
            if (sceneRuntime.lifecycleState() == Engine::SceneLifecycleState::Started) {
                sceneSchedulerStarted = true;
                return true;
            }

            if (!Engine::isValid(sceneRenderBridge->registerPreRenderSystem(sceneRenderBackend))) {
                status = "Failed to register scene render bridge scheduler system.";
                SDL_Log("%s", status.c_str());
                return false;
            }
            if (sceneAnimatedAdapter &&
                !Engine::isValid(sceneAnimatedAdapter->registerVariableAnimationSystem())) {
                status = "Failed to register scene animated adapter scheduler system.";
                SDL_Log("%s", status.c_str());
                return false;
            }
            if (sceneRuntime.lifecycleState() == Engine::SceneLifecycleState::Unloaded && !sceneRuntime.load()) {
                status = "Failed to load scene runtime scheduler.";
                SDL_Log("%s", status.c_str());
                return false;
            }
            if (sceneRuntime.lifecycleState() == Engine::SceneLifecycleState::Loaded && !sceneRuntime.start()) {
                status = "Failed to start scene runtime scheduler.";
                SDL_Log("%s", status.c_str());
                return false;
            }

            sceneSchedulerStarted = sceneRuntime.lifecycleState() == Engine::SceneLifecycleState::Started;
            return sceneSchedulerStarted;
        }

        void tickSceneScheduler(float dt)
        {
            if (sceneSchedulerStarted &&
                sceneRuntime.lifecycleState() == Engine::SceneLifecycleState::Started) {
                sceneRuntime.tickFrame(dt);
            }
        }

        void shutdown(Engine::AssetCache& assetCache)
        {
            if (sceneRuntime.lifecycleState() == Engine::SceneLifecycleState::Started) {
                sceneRuntime.stop();
            }
            if (sceneRenderBridge) {
                sceneRenderBridge->releaseRendererResources(sceneRenderBackend);
            }
            Engine::releaseSceneAuthoredAdapterResources(sceneAdapter.resources, assetCache);
            if (sceneAnimatedAdapter) {
                sceneAnimatedAdapter->releaseResources(sceneAnimatedResult.resources, assetCache);
                sceneAnimatedAdapter->unregisterVariableAnimationSystem();
                sceneAnimatedAdapter.reset();
            }
            if (sceneRenderBridge) {
                sceneRenderBridge->unregisterPreRenderSystem();
            }
            sceneRenderBridge.reset();
            if (sceneRuntime.lifecycleState() == Engine::SceneLifecycleState::Loaded) {
                sceneRuntime.unload();
            }
            usingSceneAdapter = false;
            usingSceneAnimatedAdapter = false;
            sceneSchedulerStarted = false;
            streamingScene.shutdown();
            scene.shutdown();
            fallback.shutdown();
        }
    };

    struct AnimatedSampleRuntime {
        Engine::AnimatedModel model;
        Engine::AnimationPlaybackState playback;
        Engine::AnimationCrossfadeState crossfade;
        Engine::AnimatedSkeletonPose lastPose;
        enum class AsyncPhase {
            Idle,
            CacheReadPending,
            ImportPending,
            CacheWritePending,
            ReadyToCommit,
            Committed,
            Failed,
        } asyncPhase = AsyncPhase::Idle;
        std::filesystem::path sourcePath;
        Engine::AnimatedModelLoadSettings settings;
        Engine::AnimatedModelCacheManifest cacheManifest;
        Engine::AsyncJobHandle cacheReadJob;
        Engine::AsyncJobHandle importJob;
        Engine::AsyncJobHandle cacheWriteJob;
        Engine::AnimatedModelCacheStatus cacheStatus = Engine::AnimatedModelCacheStatus::Miss;
        std::string status = "Animation sample not loaded.";
        bool enabled = false;
        bool loaded = false;
        bool placed = false;
        uint32_t asyncJobsQueued = 0;
        uint32_t asyncJobsCompleted = 0;
        uint32_t asyncJobsFailed = 0;
        float cacheReadMs = 0.0f;
        float importMs = 0.0f;
        float cacheWriteMs = 0.0f;
        std::string asyncMessage;
        uint32_t sampledFrameCount = 0;
        uint32_t failedPoseUpdateCount = 0;
        uint32_t completedCrossfadeCount = 0;

        void shutdown()
        {
            model.shutdown();
            crossfade = {};
            loaded = false;
            enabled = false;
            placed = false;
            asyncPhase = AsyncPhase::Idle;
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

    std::string requestModernCharacterNavigationTest(
        ModernDefaultSceneRuntime& runtime,
        glm::vec3 cameraPosition)
    {
        if (!Engine::isValid(runtime.playerCharacter)) {
            ++runtime.warningCount;
            return "Cannot request navigation test path: player character is invalid.";
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

        const Engine::NavigationProjectionResult projected =
            runtime.navigationService.projectPoint(cameraPosition, {}, filter);
        if (projected.status != Engine::NavigationRuntimeStatus::Success) {
            ++runtime.warningCount;
            return "Cannot request navigation test path: camera position did not project onto loaded navmesh: " +
                projected.message;
        }

        Engine::SceneCharacterPathRequest request;
        request.goal = projected.point;
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
        return "Navigation test path accepted to camera-projected goal; path points " +
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
        float verticalOffset = 24.0f)
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
        if (Engine::isValid(runtime.playerCharacter)) {
            runtime.characters.destroyCharacter(runtime.playerCharacter);
        }
        runtime.playerCharacter = createModernPlayerCharacter(runtime);
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
        std::string sceneGeometryHash = {})
    {
        Engine::OpenWorldStreamingRuntimeSettings settings;
        settings.savedBuildManifestPath = "generated/open_world_streaming/modern_default/manifest.yaml";
        const std::filesystem::path heightmapPath = resolveAuthoredScenePath(DefaultHeightmapPath);
        settings.bake.heightmap.sourcePath = heightmapPath;
        settings.bake.heightmap.sourceIdOverride = modernAssetIdForPath(heightmapPath, "modern_heightmap_streaming");
        settings.bake.heightmap.sampleSpacing = 1.0f;
        settings.bake.heightmap.heightScale = 80.0f;
        settings.bake.heightmap.heightOffset = 0.0f;
        settings.bake.heightmap.sourceOrigin = {-256.0f, 0.0f, 256.0f};
        settings.bake.heightmap.chunkWorldSize = 64.0f;
        settings.bake.heightmap.chunkResolution = 33;
        settings.bake.terrainCache.rootPath = "generated/terrain_cache/modern_default";
        settings.bake.terrainCache.policy = Engine::TerrainDerivedCachePolicy::ReadOnly;
        settings.bake.navigationCache.rootPath = "generated/navigation_cache/modern_default";
        settings.bake.navigationCache.worldId = "modern_default_streaming";
        settings.bake.navigationResolution = 17;
        settings.bake.physicsColliderResolution = 17;
        settings.bake.renderLods = {
            {0, 33, 2.0f},
            {1, 17, 2.0f},
        };
        Engine::NavAgentSettings agent;
        settings.bake.navAgent = agent;
        settings.bake.terrainNavigationBorderPaddingWorld = std::max(
            agent.radius * 2.0f,
            settings.bake.navBuild.cellSize * 4.0f);
        const float navigationStep = settings.bake.heightmap.chunkWorldSize /
            static_cast<float>(std::max(settings.bake.navigationResolution, 2u) - 1u);
        settings.bake.terrainNavigationBorderSampleCount = static_cast<uint32_t>(std::ceil(
            settings.bake.terrainNavigationBorderPaddingWorld / std::max(navigationStep, 0.0001f)));
        settings.bake.sceneGeometryHash = std::move(sceneGeometryHash);
        settings.bake.sceneGeometryMaxSlopeDegrees = agent.maxSlopeDegrees;
        settings.bake.sceneGeometryTileBoundsPadding = agent.radius + settings.bake.terrainNavigationBorderPaddingWorld;
        settings.bake.sceneGeometryAdapterVersion = "modern_scene_nav_geometry_slope_v1";

        for (Engine::StreamingPayloadResidencyPolicy& policy : settings.planner.payloadPolicies) {
            policy.activeRadius = 192.0f;
            policy.cacheRadius = 320.0f;
            policy.hysteresis = 32.0f;
            policy.maxTransitionsPerFrame = 4;
        }
        for (auto& payloadProfiles : settings.planner.profilePolicies) {
            for (Engine::StreamingPayloadResidencyPolicy& policy : payloadProfiles) {
                policy.activeRadius = 192.0f;
                policy.cacheRadius = 320.0f;
                policy.hysteresis = 32.0f;
                policy.maxTransitionsPerFrame = 4;
            }
        }
        for (uint32_t payload = 0; payload < Engine::StreamingPayloadKindCount; ++payload) {
            settings.planner.profilePolicies[payload][static_cast<uint32_t>(Engine::StreamingHaloProfile::HighDetailLive)]
                .activeRadius = 96.0f;
            settings.planner.profilePolicies[payload][static_cast<uint32_t>(Engine::StreamingHaloProfile::HighDetailLive)]
                .cacheRadius = 192.0f;
            settings.planner.profilePolicies[payload][static_cast<uint32_t>(Engine::StreamingHaloProfile::CacheOnly)]
                .liveAllowed = false;
            settings.planner.profilePolicies[payload][static_cast<uint32_t>(Engine::StreamingHaloProfile::FarMetadata)]
                .liveAllowed = false;
            settings.planner.profilePolicies[payload][static_cast<uint32_t>(Engine::StreamingHaloProfile::FarMetadata)]
                .cacheRadius = 512.0f;
        }
        settings.cache.maxReadJobsQueuedPerUpdate = 8;
        settings.cache.maxCompletedJobsMergedPerUpdate = 16;
        settings.generation.policy = Engine::StreamingDerivedGenerationPolicy::ReadOnly;
        settings.promotion.maxPromotesQueuedPerUpdate = 8;
        settings.promotion.maxDemotesQueuedPerUpdate = 8;
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
        Engine::AssetCache& assetCache)
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

        Engine::OpenWorldStreamingRuntimeSettings settings = modernStreamingRuntimeSettings();
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

    std::unique_ptr<ModernDefaultSceneRuntime> startModernDefaultSceneRuntime(Engine::AssetCache& assetCache)
    {
        auto runtime = std::make_unique<ModernDefaultSceneRuntime>();
        runtime->characters.setNavigationService(&runtime->navigationService);
        Engine::NavigationConnectivitySettings connectivitySettings = runtime->navigationConnectivity.settings();
        connectivitySettings.requireCenterReachability = false;
        runtime->navigationConnectivity.setSettings(connectivitySettings);
        runtime->bounds = {{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()},
            {-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()}};

        runtime->usingOpenWorldStreaming = initializeModernStreamingTerrain(*runtime, assetCache);
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
            ReleaseKayKitAnimatedModelPath,
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

        runtime->playerActor = runtime->scene.createActor({0x4d4f4445524e01ull});
        Engine::SceneTransform playerTransform;
        playerTransform.translation = modernTerrainRelativePosition(*runtime, focus, {0.0f, 24.0f, -30.0f});
        runtime->scene.setLocalTransform(runtime->playerActor, playerTransform);
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

    std::string_view animatedAsyncPhaseName(AnimatedSampleRuntime::AsyncPhase phase);

    void placeAnimatedSampleNearBounds(AnimatedSampleRuntime& runtime, const Renderer::Aabb& bounds)
    {
        if (!runtime.loaded) {
            return;
        }

        const glm::vec3 center = (bounds.min + bounds.max) * 0.5f;
        const glm::vec3 extents = glm::max(bounds.max - bounds.min, glm::vec3{2.0f});
        const float xOffset = std::clamp(glm::length(extents) * 0.08f, 1.5f, 8.0f);
        const glm::vec3 position{center.x + xOffset, bounds.min.y + 1.0f, center.z};
        const glm::mat4 transform = glm::translate(glm::mat4{1.0f}, position);
        for (uint32_t index = 0; index < runtime.model.skinnedInstanceCount(); ++index) {
            const std::optional<Engine::AnimatedModelSkinnedInstance> instance = runtime.model.skinnedInstance(index);
            if (instance) {
                Renderer::setSkinnedInstanceTransform(instance->handle, transform);
            }
        }
    }

    void placeSceneAnimatedSampleNearBounds(AuthoredRuntime& runtime)
    {
        if (!runtime.usingSceneAnimatedAdapter || runtime.sceneAnimatedPlaced) {
            return;
        }

        const glm::vec3 center = (runtime.bounds.min + runtime.bounds.max) * 0.5f;
        const glm::vec3 extents = glm::max(runtime.bounds.max - runtime.bounds.min, glm::vec3{2.0f});
        const float sceneRadius = std::max(glm::length(extents) * 0.5f, 2.0f);
        const float heightOffset = std::clamp(sceneRadius * 0.08f, 4.0f, 18.0f);
        const float sampleScale = std::clamp(sceneRadius * 0.12f, 8.0f, 60.0f);
        const glm::vec3 position{center.x, runtime.bounds.max.y + heightOffset, center.z};
        for (const Engine::SceneAnimatedNodeBinding& binding : runtime.sceneAnimatedResult.nodes) {
            if (!runtime.sceneRuntime.contains(binding.actor) || runtime.sceneRuntime.parent(binding.actor).has_value()) {
                continue;
            }

            std::optional<Engine::SceneTransform> transform = runtime.sceneRuntime.localTransform(binding.actor);
            if (!transform) {
                continue;
            }
            transform->translation += position;
            transform->scale *= sampleScale;
            runtime.sceneRuntime.setLocalTransform(binding.actor, *transform);
        }
        runtime.sceneAnimatedFocus = position + glm::vec3{0.0f, sampleScale * 1.25f, 0.0f};
        runtime.sceneAnimatedCameraDistance = std::max(sampleScale * 8.0f, 35.0f);
        runtime.bounds.max.y = std::max(runtime.bounds.max.y, position.y + sampleScale * 3.0f);
        runtime.sceneAnimatedPlaced = true;
    }

    void frameCameraForSceneAnimatedSample(Engine::OrbitCameraController& camera, const AuthoredRuntime& runtime)
    {
        if (!runtime.usingSceneAnimatedAdapter || !runtime.sceneAnimatedPlaced) {
            return;
        }

        Engine::CameraSettings settings = camera.settings();
        settings.nearPlane = 0.05f;
        settings.farPlane = std::max(runtime.sceneAnimatedCameraDistance * 8.0f, 1000.0f);
        settings.maxDistance = std::max(runtime.sceneAnimatedCameraDistance * 2.0f, 120.0f);
        const float padding = std::max(runtime.sceneAnimatedCameraDistance, 50.0f);
        settings.minPivotXZ = {runtime.sceneAnimatedFocus.x - padding, runtime.sceneAnimatedFocus.z - padding};
        settings.maxPivotXZ = {runtime.sceneAnimatedFocus.x + padding, runtime.sceneAnimatedFocus.z + padding};
        settings.edgePanSpeed = std::max(runtime.sceneAnimatedCameraDistance * 0.35f, 20.0f);
        settings.mousePanSensitivity = 0.12f;
        settings.zoomSensitivity = std::max(runtime.sceneAnimatedCameraDistance * 0.08f, 8.0f);
        camera.settings() = settings;

        Engine::CameraState state;
        state.mode = Engine::CameraMode::Free;
        state.pivot = runtime.sceneAnimatedFocus;
        state.yawRadians = glm::radians(35.0f);
        state.pitchRadians = glm::radians(-28.0f);
        state.distance = std::clamp(runtime.sceneAnimatedCameraDistance, settings.minDistance, settings.maxDistance);
        camera.setState(state);
    }

    void applyReleaseSceneAnimatedKayKitTextureMaterial(
        AuthoredRuntime& runtime,
        Engine::AssetCache& assetCache)
    {
        const std::filesystem::path texturePath = resolveAuthoredScenePath(ReleaseKayKitKnightTexturePath);
        if (!std::filesystem::exists(texturePath)) {
            SDL_Log("Release KayKit texture unavailable: %s", texturePath.generic_string().c_str());
            return;
        }

        Renderer::TextureDescriptor textureDescriptor;
        textureDescriptor.slot = Renderer::TextureSlot::BaseColor;
        textureDescriptor.colorSpace = Renderer::TextureColorSpace::Srgb;
        textureDescriptor.wrapU = Renderer::TextureWrap::Repeat;
        textureDescriptor.wrapV = Renderer::TextureWrap::Repeat;
        textureDescriptor.minFilter = Renderer::TextureFilter::Linear;
        textureDescriptor.magFilter = Renderer::TextureFilter::Linear;
        textureDescriptor.mipFilter = Renderer::TextureFilter::Linear;
        textureDescriptor.generateMips = true;
        textureDescriptor.debugName = "ReleaseKayKitKnightBaseColor";

        Engine::CachedTexture texture = assetCache.acquireTexture(texturePath, textureDescriptor);
        if (!Renderer::isValid(texture.handle)) {
            SDL_Log("Failed to acquire release KayKit texture: %s", texturePath.generic_string().c_str());
            return;
        }
        runtime.sceneAnimatedResult.resources.textures.push_back(texture);

        uint32_t materialIndex = 0;
        for (Renderer::MaterialHandle material : runtime.sceneAnimatedResult.resources.materials) {
            if (material.id == UINT32_MAX) {
                continue;
            }

            Renderer::MaterialDescriptor descriptor;
            descriptor.name = "ReleaseKayKitKnightMaterial." + std::to_string(materialIndex);
            descriptor.baseColorFactor = glm::vec4{1.0f};
            descriptor.baseColorTexture = texture.handle;
            descriptor.metallicFactor = 0.0f;
            descriptor.roughnessFactor = 0.65f;
            descriptor.alphaMode = Renderer::MaterialDescriptor::AlphaMode::Opaque;
            descriptor.doubleSided = true;
            Renderer::setMaterialDescriptor(material, descriptor);
            ++materialIndex;
        }

        SDL_Log(
            "Applied release KayKit knight texture to %u animated materials: %s",
            materialIndex,
            texturePath.generic_string().c_str());
    }

    void startReleaseSceneAnimatedSampleRuntime(
        AuthoredRuntime& runtime,
        Engine::AssetCache& assetCache,
        const std::filesystem::path& path)
    {
        if (!runtime.sceneRenderBridge) {
            return;
        }

        const std::filesystem::path resolvedPath = resolveAuthoredScenePath(path);
        if (!std::filesystem::exists(resolvedPath)) {
            SDL_Log("Release scene animated adapter sample unavailable: %s", resolvedPath.generic_string().c_str());
            return;
        }

        const Assets::Assimp::ImportedScene importedScene = Assets::Assimp::importScene(resolvedPath);
        if (!importedScene.success) {
            SDL_Log("Failed to import release animated adapter sample: %s", importedScene.error.c_str());
            return;
        }

        runtime.sceneAnimatedAdapter =
            std::make_unique<Engine::SceneAnimatedModelAdapter>(runtime.sceneRuntime, *runtime.sceneRenderBridge);
        Engine::SceneAnimatedAdapterSettings settings;
        settings.loadTextures = true;
        settings.createSkinnedMeshes = true;
        settings.renderLayer = Renderer::RenderLayer::Props;
        settings.maxDrawDistance = 0.0f;
        settings.defaultClipIndex = 0;
        settings.playOnStart = true;
        settings.loop = true;
        settings.playbackSpeed = 1.0f;
        settings.allowRootFallbackSkinnedBindings = true;
        settings.materialNamePrefix = "ReleaseKayKitAnimatedMaterial";
        settings.textureDebugNamePrefix = "ReleaseKayKitAnimated";
        runtime.sceneAnimatedResult = runtime.sceneAnimatedAdapter->adaptImportedScene(
            importedScene,
            resolvedPath,
            assetCache,
            settings);
        if (!runtime.sceneAnimatedResult.success) {
            SDL_Log("Failed to adapt release animated sample through scene adapter: %s",
                runtime.sceneAnimatedResult.message.c_str());
            runtime.sceneAnimatedAdapter->releaseResources(runtime.sceneAnimatedResult.resources, assetCache);
            runtime.sceneAnimatedAdapter.reset();
            return;
        }

        runtime.usingSceneAnimatedAdapter = true;
        applyReleaseSceneAnimatedKayKitTextureMaterial(runtime, assetCache);
        placeSceneAnimatedSampleNearBounds(runtime);
        const Engine::SceneRenderBridgeDiagnostics bridgeDiagnostics = runtime.sceneRenderBridge->diagnostics();
        uint32_t validSkinnedResourceCount = 0;
        std::string skinnedResourceIds;
        for (Renderer::SkinnedMeshHandle mesh : runtime.sceneAnimatedResult.resources.skinnedMeshes) {
            if (!skinnedResourceIds.empty()) {
                skinnedResourceIds += ",";
            }
            skinnedResourceIds += std::to_string(mesh.id);
            if (mesh.id != UINT32_MAX) {
                ++validSkinnedResourceCount;
            }
        }
        SDL_Log(
            "Loaded release KayKit animated sample through scene adapter: %s (nodes %u skins %u skinnedMeshes %u validSkinnedResources %u resourceSlots %zu resourceIds [%s] components %u clips %u liveSkinnedInstances %u invalidNodes %u invalidMeshes %u invalidSkins %u invalidJoints %u invalidMaterials %u warnings %zu)",
            resolvedPath.generic_string().c_str(),
            runtime.sceneAnimatedResult.diagnostics.createdActorCount,
            runtime.sceneAnimatedResult.diagnostics.createdSkeletonCount,
            runtime.sceneAnimatedResult.diagnostics.createdSkinnedMeshCount,
            validSkinnedResourceCount,
            runtime.sceneAnimatedResult.resources.skinnedMeshes.size(),
            skinnedResourceIds.c_str(),
            runtime.sceneAnimatedResult.diagnostics.createdSkinnedComponentCount,
            runtime.sceneAnimatedResult.diagnostics.importedAnimationCount,
            bridgeDiagnostics.liveSkinnedInstanceCount,
            runtime.sceneAnimatedResult.diagnostics.invalidNodeReferenceCount,
            runtime.sceneAnimatedResult.diagnostics.invalidMeshReferenceCount,
            runtime.sceneAnimatedResult.diagnostics.invalidSkinReferenceCount,
            runtime.sceneAnimatedResult.diagnostics.invalidJointReferenceCount,
            runtime.sceneAnimatedResult.diagnostics.invalidMaterialReferenceCount,
            runtime.sceneAnimatedResult.diagnostics.warnings.size());
        for (const std::string& warning : runtime.sceneAnimatedResult.diagnostics.warnings) {
            SDL_Log("Release scene animated adapter warning: %s", warning.c_str());
        }
    }

    AnimatedSampleRuntime startAnimatedSampleRuntime(
        const std::filesystem::path& path,
        Engine::AsyncWorkQueue& asyncWork)
    {
        AnimatedSampleRuntime runtime;
        const std::filesystem::path resolvedPath = resolveAuthoredScenePath(path);
        runtime.sourcePath = resolvedPath;
        if (!std::filesystem::exists(resolvedPath)) {
            runtime.status = "Animated model unavailable: " + resolvedPath.generic_string();
            SDL_Log("%s", runtime.status.c_str());
            return runtime;
        }

        runtime.settings.loadTextures = true;
        runtime.settings.createBindPoseInstances = false;
        runtime.settings.createSkinnedMeshes = true;
        runtime.settings.createSkinnedInstances = true;
        runtime.settings.renderLayer = Renderer::RenderLayer::Props;
        runtime.settings.cache.policy = Engine::AnimatedModelCachePolicy::GenerateOnMiss;
        runtime.cacheManifest = Engine::AnimatedModelCache::buildManifest(runtime.settings.cache, resolvedPath);
        runtime.cacheReadJob = Engine::enqueueAnimatedModelCacheRead(asyncWork, runtime.cacheManifest);
        if (runtime.cacheReadJob.id == UINT64_MAX) {
            runtime.status = "Failed to enqueue animated model cache read.";
            runtime.asyncPhase = AnimatedSampleRuntime::AsyncPhase::Failed;
            SDL_Log("%s", runtime.status.c_str());
            return runtime;
        }
        runtime.asyncPhase = AnimatedSampleRuntime::AsyncPhase::CacheReadPending;
        runtime.status = "Animated model cache read pending: " + resolvedPath.generic_string();
        runtime.asyncMessage = runtime.status;
        ++runtime.asyncJobsQueued;
        return runtime;
    }

    void commitAnimatedPayload(
        AnimatedSampleRuntime& runtime,
        Engine::AssetCache& assetCache,
        Engine::AnimatedModelCachePayload payload,
        bool fromCache)
    {
        Engine::AnimatedModelLoadResult result = Engine::createAnimatedModelFromPayload(
            runtime.sourcePath,
            std::move(payload),
            assetCache,
            runtime.settings);
        if (!result.success) {
            runtime.status = "Failed to commit animated model: " + result.message;
            runtime.asyncMessage = runtime.status;
            runtime.asyncPhase = AnimatedSampleRuntime::AsyncPhase::Failed;
            ++runtime.asyncJobsFailed;
            SDL_Log("%s", runtime.status.c_str());
            return;
        }

        runtime.model = std::move(result.model);
        runtime.loaded = true;
        runtime.enabled = runtime.model.skinnedInstanceCount() > 0 && runtime.model.clipCount() > 0;
        runtime.playback.clipIndex = 0;
        runtime.playback.timeSeconds = 0.0f;
        runtime.playback.speed = 1.0f;
        runtime.playback.loop = true;
        runtime.playback.playing = runtime.enabled;
        runtime.asyncPhase = AnimatedSampleRuntime::AsyncPhase::Committed;
        runtime.status = runtime.enabled
            ? "Animated model loaded: " + runtime.sourcePath.generic_string()
            : "Animated model loaded without playable skinned instances or clips.";
        runtime.asyncMessage = runtime.status;
        SDL_Log("%s", runtime.status.c_str());
        SDL_Log("Animated model diagnostics: %s",
            Engine::animatedModelDiagnosticsSummaryText(runtime.model.diagnostics()).c_str());
    }

    void enqueueAnimatedImport(AnimatedSampleRuntime& runtime, Engine::AsyncWorkQueue& asyncWork)
    {
        runtime.importJob = Engine::enqueueAnimatedModelImportAndPreparePayload(asyncWork, runtime.sourcePath);
        if (runtime.importJob.id == UINT64_MAX) {
            runtime.status = "Failed to enqueue animated model import.";
            runtime.asyncMessage = runtime.status;
            runtime.asyncPhase = AnimatedSampleRuntime::AsyncPhase::Failed;
            ++runtime.asyncJobsFailed;
            SDL_Log("%s", runtime.status.c_str());
            return;
        }
        runtime.asyncPhase = AnimatedSampleRuntime::AsyncPhase::ImportPending;
        runtime.status = "Animated model source import pending: " + runtime.sourcePath.generic_string();
        runtime.asyncMessage = runtime.status;
        ++runtime.asyncJobsQueued;
    }

    void processAnimatedAsyncCompletion(
        AnimatedSampleRuntime& runtime,
        Engine::AssetCache& assetCache,
        Engine::AsyncWorkQueue& asyncWork,
        Engine::AsyncCompletedJob& completed)
    {
        if (Engine::AnimatedModelCacheReadJobResult* cacheRead =
                std::any_cast<Engine::AnimatedModelCacheReadJobResult>(&completed.result)) {
            ++runtime.asyncJobsCompleted;
            runtime.cacheReadMs = cacheRead->readMs;
            runtime.cacheStatus = cacheRead->result.status;
            runtime.asyncMessage = cacheRead->result.message;
            if (cacheRead->result.status == Engine::AnimatedModelCacheStatus::Hit && cacheRead->result.payload) {
                commitAnimatedPayload(runtime, assetCache, std::move(*cacheRead->result.payload), true);
                return;
            }
            if (cacheRead->result.status == Engine::AnimatedModelCacheStatus::Cancelled) {
                runtime.status = "Animated model cache read cancelled.";
                runtime.asyncPhase = AnimatedSampleRuntime::AsyncPhase::Failed;
                return;
            }
            enqueueAnimatedImport(runtime, asyncWork);
            return;
        }

        if (Engine::AnimatedModelImportJobResult* import =
                std::any_cast<Engine::AnimatedModelImportJobResult>(&completed.result)) {
            ++runtime.asyncJobsCompleted;
            runtime.importMs = import->importMs;
            runtime.asyncMessage = import->message;
            if (import->cancelled || !import->success) {
                runtime.status = "Failed to import animated model asynchronously: " + import->message;
                runtime.asyncPhase = AnimatedSampleRuntime::AsyncPhase::Failed;
                ++runtime.asyncJobsFailed;
                SDL_Log("%s", runtime.status.c_str());
                return;
            }

            if (runtime.settings.cache.policy == Engine::AnimatedModelCachePolicy::GenerateOnMiss ||
                runtime.settings.cache.policy == Engine::AnimatedModelCachePolicy::Refresh) {
                Engine::AnimatedModelCachePayload writePayload = import->payload;
                runtime.cacheWriteJob = Engine::enqueueAnimatedModelCacheWrite(
                    asyncWork,
                    runtime.cacheManifest,
                    std::move(writePayload));
                if (runtime.cacheWriteJob.id != UINT64_MAX) {
                    runtime.asyncPhase = AnimatedSampleRuntime::AsyncPhase::CacheWritePending;
                    ++runtime.asyncJobsQueued;
                }
            }

            commitAnimatedPayload(runtime, assetCache, std::move(import->payload), false);
            return;
        }

        if (Engine::AnimatedModelCacheWriteJobResult* cacheWrite =
                std::any_cast<Engine::AnimatedModelCacheWriteJobResult>(&completed.result)) {
            ++runtime.asyncJobsCompleted;
            runtime.cacheWriteMs = cacheWrite->writeMs;
            runtime.cacheStatus = cacheWrite->result.status;
            runtime.asyncMessage = cacheWrite->result.message;
            if (cacheWrite->result.status != Engine::AnimatedModelCacheStatus::WriteSuccess &&
                cacheWrite->result.status != Engine::AnimatedModelCacheStatus::Cancelled) {
                ++runtime.asyncJobsFailed;
                SDL_Log("Animated model cache write failed: %s", cacheWrite->result.message.c_str());
            }
            if (runtime.loaded) {
                runtime.asyncMessage = cacheWrite->result.message;
            }
        }
    }

    void updateAnimatedSampleRuntime(AnimatedSampleRuntime& runtime, float dt)
    {
        if (!runtime.enabled || !runtime.loaded || runtime.model.clipCount() == 0) {
            return;
        }

        if (runtime.playback.clipIndex >= runtime.model.clipCount()) {
            runtime.playback.clipIndex = 0;
            runtime.playback.timeSeconds = 0.0f;
        }
        const bool wasCrossfading = runtime.crossfade.active;
        if (runtime.crossfade.active) {
            runtime.lastPose = Engine::advanceCrossfade(runtime.model, runtime.crossfade, runtime.playback, dt);
            if (wasCrossfading && !runtime.crossfade.active) {
                ++runtime.completedCrossfadeCount;
            }
        } else {
            runtime.model.advancePlayback(runtime.playback, dt);
            runtime.lastPose = runtime.model.sampleClip(
                runtime.playback.clipIndex,
                runtime.playback.timeSeconds,
                {runtime.playback.loop, std::nullopt});
        }
        if (!runtime.lastPose.diagnostics.valid) {
            ++runtime.failedPoseUpdateCount;
            return;
        }

        bool allUpdated = true;
        for (uint32_t index = 0; index < runtime.model.skinnedInstanceCount(); ++index) {
            allUpdated = runtime.model.updateSkinnedPose(index, runtime.lastPose) && allUpdated;
        }
        if (allUpdated) {
            ++runtime.sampledFrameCount;
        } else {
            ++runtime.failedPoseUpdateCount;
        }
    }

    void populateAnimatedDebugSettings(
        Renderer::DebugUi::RendererDebugSettings& settings,
        const AnimatedSampleRuntime& runtime)
    {
        (void)settings;
        (void)runtime;
    }

    void applyAnimatedDebugSettings(
        AnimatedSampleRuntime& runtime,
        const Renderer::DebugUi::RendererDebugSettings& settings)
    {
        (void)runtime;
        (void)settings;
    }

    std::string_view authoredAsyncPhaseName(AuthoredRuntime::AsyncPhase phase)
    {
        switch (phase) {
            case AuthoredRuntime::AsyncPhase::CacheReadPending:
                return "cache read pending";
            case AuthoredRuntime::AsyncPhase::ImportPending:
                return "source import pending";
            case AuthoredRuntime::AsyncPhase::CacheWritePending:
                return "cache write pending";
            case AuthoredRuntime::AsyncPhase::Committed:
                return "committed";
            case AuthoredRuntime::AsyncPhase::Failed:
                return "failed";
            case AuthoredRuntime::AsyncPhase::Idle:
            default:
                return "idle";
        }
    }

    std::string_view animatedAsyncPhaseName(AnimatedSampleRuntime::AsyncPhase phase)
    {
        switch (phase) {
            case AnimatedSampleRuntime::AsyncPhase::CacheReadPending:
                return "cache read pending";
            case AnimatedSampleRuntime::AsyncPhase::ImportPending:
                return "source import pending";
            case AnimatedSampleRuntime::AsyncPhase::CacheWritePending:
                return "cache write pending";
            case AnimatedSampleRuntime::AsyncPhase::ReadyToCommit:
                return "ready to commit";
            case AnimatedSampleRuntime::AsyncPhase::Committed:
                return "committed";
            case AnimatedSampleRuntime::AsyncPhase::Failed:
                return "failed";
            case AnimatedSampleRuntime::AsyncPhase::Idle:
            default:
                return "idle";
        }
    }

    void logAuthoredDiagnostics(const Engine::AuthoredSceneDiagnostics& diagnostics)
    {
        const std::string summary = Engine::authoredSceneDiagnosticsSummaryText(diagnostics);
        SDL_Log("Authored scene diagnostics: %s", summary.c_str());
        for (const std::string& warning : diagnostics.warnings) {
            SDL_Log("Authored scene warning: %s", warning.c_str());
        }
    }

    void populateAuthoredDebugSettings(
        Renderer::DebugUi::RendererDebugSettings& settings,
        const std::filesystem::path& path,
        const Engine::AuthoredSceneDiagnostics& diagnostics)
    {
        (void)settings;
        (void)path;
        (void)diagnostics;
    }

    Renderer::Aabb authoredBoundsOrFallback(const Engine::AuthoredSceneBounds& bounds)
    {
        if (!bounds.valid) {
            return {{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}};
        }
        return {bounds.min, bounds.max};
    }

    Renderer::Aabb importedBoundsOrFallback(const Assets::Assimp::ImportedSceneBounds& bounds)
    {
        if (!bounds.valid) {
            return {{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}};
        }
        return {bounds.min, bounds.max};
    }

    void frameCameraForBounds(Engine::OrbitCameraController& camera, const Renderer::Aabb& bounds)
    {
        const glm::vec3 center = (bounds.min + bounds.max) * 0.5f;
        const glm::vec3 extents = glm::max(bounds.max - bounds.min, glm::vec3{2.0f});
        const float radius = std::max(glm::length(extents) * 0.5f, 2.0f);
        Engine::CameraSettings settings = camera.settings();
        const float padding = std::max(radius * 0.5f, 10.0f);
        settings.nearPlane = 0.05f;
        settings.farPlane = std::max(radius * 8.0f, 500.0f);
        settings.maxDistance = std::max(radius * 3.0f, 80.0f);
        settings.minPivotXZ = {bounds.min.x - padding, bounds.min.z - padding};
        settings.maxPivotXZ = {bounds.max.x + padding, bounds.max.z + padding};
        settings.edgePanSpeed = std::max(radius * 0.35f, 20.0f);
        settings.mousePanSensitivity = std::max(radius * 0.0015f, 0.08f);
        settings.zoomSensitivity = std::max(radius * 0.08f, 8.0f);
        camera.settings() = settings;

        Engine::CameraState state;
        state.mode = Engine::CameraMode::Free;
        state.pivot = center;
        state.yawRadians = glm::radians(35.0f);
        state.pitchRadians = glm::radians(-28.0f);
        state.distance = std::clamp(radius * 1.55f, settings.minDistance, settings.maxDistance);
        camera.setState(state);
    }

    void applyAuthoredAtmosphereDefaults(Renderer::AtmosphereSettings& atmosphere)
    {
        atmosphere.skyColor = {0.08f, 0.09f, 0.11f, 1.0f};
        atmosphere.fogColor = {0.16f, 0.17f, 0.18f, 1.0f};
        atmosphere.fogEnabled = false;
        atmosphere.sunDirection = {-0.35f, -0.75f, -0.25f};
        atmosphere.sunColor = {1.0f, 0.95f, 0.86f};
        atmosphere.sunIntensity = 2.0f;
        atmosphere.exposure = 1.1f;
        atmosphere.ambientIntensity = 0.12f;
        atmosphere.environmentDiffuseColor = {0.85f, 0.9f, 1.0f};
        atmosphere.environmentDiffuseIntensity = 1.0f;
        atmosphere.environmentEnabled = true;
    }

    Engine::AuthoredSceneStreamingSettings defaultAuthoredSceneStreamingSettings()
    {
        Engine::AuthoredSceneStreamingSettings settings;
        settings.load.loadTextures = true;
        settings.cache.policy = Engine::AuthoredSceneCachePolicy::ReadOnly;
        settings.partition.sectorSize = 25.0f;
        settings.loadRadius = 45.0f;
        settings.unloadRadius = 75.0f;
        settings.maxSectorLoadCommitsPerFrame = 1;
        settings.maxSectorUnloadCommitsPerFrame = 2;
        settings.loadInitialSectorsImmediately = false;
        return settings;
    }

    void enqueueAuthoredImport(AuthoredRuntime& runtime, Engine::AsyncWorkQueue& asyncWork)
    {
        runtime.importJob = Engine::enqueueAuthoredSceneImportAndPartition(
            asyncWork,
            runtime.sourcePath,
            runtime.settings.partition);
        if (runtime.importJob.id != UINT64_MAX) {
            runtime.asyncPhase = AuthoredRuntime::AsyncPhase::ImportPending;
            ++runtime.asyncJobsQueued;
            runtime.status = "Authored scene source import pending: " + runtime.sourcePath.generic_string();
        } else {
            runtime.asyncPhase = AuthoredRuntime::AsyncPhase::Failed;
            runtime.usingFallback = true;
            runtime.status = "Failed to enqueue authored scene source import.";
        }
    }

    AuthoredRuntime startAuthoredRuntime(
        const std::filesystem::path& path,
        Engine::AssetCache& assetCache,
        Engine::AsyncWorkQueue& asyncWork)
    {
        AuthoredRuntime runtime;
        const std::filesystem::path resolvedPath = resolveAuthoredScenePath(path);
        if (std::filesystem::exists(resolvedPath)) {
            runtime.sourcePath = resolvedPath;
            if constexpr (!BuildDebugToolsEnabled) {
                const Assets::Assimp::ImportedScene importedScene = Assets::Assimp::importScene(resolvedPath);
                if (!importedScene.success) {
                    runtime.usingFallback = true;
                    runtime.fallback.mesh = Renderer::createTexturedCubeMesh();
                    runtime.fallback.instance = Renderer::createInstance(runtime.fallback.mesh);
                    runtime.bounds = {{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}};
                    runtime.status = "Failed to import authored scene for scene adapter: " + importedScene.error;
                    SDL_Log("%s", runtime.status.c_str());
                    return runtime;
                }

                runtime.sceneRenderBridge = std::make_unique<Engine::SceneRenderBridge>(runtime.sceneRuntime);
                Engine::SceneAuthoredAdapterSettings adapterSettings;
                adapterSettings.loadTextures = true;
                adapterSettings.renderLayer = Renderer::RenderLayer::Props;
                adapterSettings.maxDrawDistance = 0.0f;
                adapterSettings.materialNamePrefix = "ReleaseAuthoredMaterial";
                adapterSettings.textureDebugNamePrefix = "ReleaseAuthored";
                runtime.sceneAdapter = Engine::adaptImportedSceneToScene(
                    importedScene,
                    resolvedPath,
                    assetCache,
                    runtime.sceneRuntime,
                    *runtime.sceneRenderBridge,
                    adapterSettings);
                if (!runtime.sceneAdapter.success) {
                    runtime.sceneRenderBridge.reset();
                    Engine::releaseSceneAuthoredAdapterResources(runtime.sceneAdapter.resources, assetCache);
                    runtime.usingFallback = true;
                    runtime.fallback.mesh = Renderer::createTexturedCubeMesh();
                    runtime.fallback.instance = Renderer::createInstance(runtime.fallback.mesh);
                    runtime.bounds = {{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}};
                    runtime.status = "Failed to adapt authored scene to scene runtime: " + runtime.sceneAdapter.message;
                    SDL_Log("%s", runtime.status.c_str());
                    return runtime;
                }

                runtime.usingSceneAdapter = true;
                runtime.bounds = importedBoundsOrFallback(importedScene.bounds);
                runtime.frameSceneAfterCommit = true;
                runtime.asyncPhase = AuthoredRuntime::AsyncPhase::Committed;
                runtime.status = "Loaded authored scene through scene adapter: " + resolvedPath.generic_string();
                SDL_Log("%s", runtime.status.c_str());
                SDL_Log(
                    "Scene authored adapter diagnostics: nodes %u meshes %u materials %u meshComponents %u lights %u warnings %zu",
                    runtime.sceneAdapter.diagnostics.createdActorCount,
                    runtime.sceneAdapter.diagnostics.createdRendererMeshCount,
                    runtime.sceneAdapter.diagnostics.createdMaterialCount,
                    runtime.sceneAdapter.diagnostics.createdMeshComponentCount,
                    runtime.sceneAdapter.diagnostics.createdLightComponentCount,
                    runtime.sceneAdapter.diagnostics.warnings.size());
                for (const std::string& warning : runtime.sceneAdapter.diagnostics.warnings) {
                    SDL_Log("Scene authored adapter warning: %s", warning.c_str());
                }
                return runtime;
            }

            runtime.settings = defaultAuthoredSceneStreamingSettings();
            runtime.cacheManifest = Engine::AuthoredSceneCache::buildManifest(
                runtime.settings.cache,
                resolvedPath,
                runtime.settings.partition);
            runtime.showingPlaceholder = true;
            runtime.fallback.mesh = Renderer::createTexturedCubeMesh();
            runtime.fallback.instance = Renderer::createInstance(runtime.fallback.mesh);
            runtime.bounds = {{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}};

            if (runtime.settings.cache.policy != Engine::AuthoredSceneCachePolicy::Disabled &&
                runtime.settings.cache.policy != Engine::AuthoredSceneCachePolicy::Refresh) {
                runtime.cacheReadJob = Engine::enqueueAuthoredSceneCacheRead(asyncWork, runtime.cacheManifest);
                if (runtime.cacheReadJob.id != UINT64_MAX) {
                    runtime.asyncPhase = AuthoredRuntime::AsyncPhase::CacheReadPending;
                    ++runtime.asyncJobsQueued;
                    runtime.status = "Authored scene cache read pending: " + resolvedPath.generic_string();
                    SDL_Log("%s", runtime.status.c_str());
                    return runtime;
                }
            }

            enqueueAuthoredImport(runtime, asyncWork);
            SDL_Log("%s", runtime.status.c_str());
            return runtime;
        } else {
            std::error_code error;
            const std::filesystem::path currentPath = std::filesystem::current_path(error);
            SDL_Log("Authored scene file is missing: %s (resolved: %s, cwd: %s, base: %s)",
                path.string().c_str(),
                resolvedPath.string().c_str(),
                error ? "<unavailable>" : currentPath.string().c_str(),
                executableBasePath().string().c_str());
        }

        runtime.usingFallback = true;
        runtime.fallback.mesh = Renderer::createTexturedCubeMesh();
        runtime.fallback.instance = Renderer::createInstance(runtime.fallback.mesh);
        runtime.bounds = {{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}};
        runtime.status = "Authored scene unavailable; showing fallback cube for " + resolvedPath.generic_string();
        SDL_Log("%s", runtime.status.c_str());
        return runtime;
    }

    void commitAuthoredPayload(
        AuthoredRuntime& runtime,
        Engine::AssetCache& assetCache,
        Engine::AuthoredSceneCachePayload payload,
        bool loadedFromCache)
    {
        Engine::PartitionedAuthoredSceneLoadResult result =
            Engine::createPartitionedAuthoredSceneFromPayload(
                runtime.sourcePath,
                std::move(payload),
                assetCache,
                runtime.settings);
        if (!result.success) {
            runtime.asyncPhase = AuthoredRuntime::AsyncPhase::Failed;
            runtime.usingFallback = true;
            runtime.status = "Failed to commit authored scene payload: " + result.message;
            SDL_Log("%s", runtime.status.c_str());
            return;
        }

        runtime.streamingScene = std::move(result.scene);
        runtime.usingStreaming = true;
        runtime.showingPlaceholder = false;
        runtime.fallback.shutdown();
        runtime.bounds = authoredBoundsOrFallback(runtime.streamingScene.bounds());
        runtime.frameSceneAfterCommit = true;
        runtime.asyncPhase = AuthoredRuntime::AsyncPhase::Committed;
        runtime.status = loadedFromCache
            ? "Loaded streaming authored scene from cache: " + runtime.sourcePath.generic_string()
            : "Loaded streaming authored scene from source: " + runtime.sourcePath.generic_string();
        runtime.streamingScene.setAsyncDiagnostics(
            std::string{authoredAsyncPhaseName(runtime.asyncPhase)},
            runtime.asyncJobsQueued,
            runtime.asyncJobsCompleted,
            runtime.asyncJobsFailed,
            0,
            runtime.cacheReadMs,
            runtime.importMs,
            runtime.cacheWriteMs,
            runtime.status);
        logAuthoredDiagnostics(runtime.streamingScene.diagnostics());
        SDL_Log("%s", runtime.status.c_str());
    }

    void failAuthoredAsyncRuntime(AuthoredRuntime& runtime, std::string message)
    {
        runtime.asyncPhase = AuthoredRuntime::AsyncPhase::Failed;
        runtime.usingFallback = true;
        runtime.showingPlaceholder = false;
        ++runtime.asyncJobsFailed;
        runtime.status = std::move(message);
        SDL_Log("%s", runtime.status.c_str());
    }

    void updateAuthoredAsyncDiagnostics(AuthoredRuntime& runtime, uint32_t pendingJobs, std::string_view message)
    {
        if (!runtime.usingStreaming) {
            return;
        }

        runtime.streamingScene.setAsyncDiagnostics(
            std::string{authoredAsyncPhaseName(runtime.asyncPhase)},
            runtime.asyncJobsQueued,
            runtime.asyncJobsCompleted,
            runtime.asyncJobsFailed,
            pendingJobs,
            runtime.cacheReadMs,
            runtime.importMs,
            runtime.cacheWriteMs,
            std::string{message});
    }

    void processAuthoredAsyncCompletion(
        AuthoredRuntime& runtime,
        Engine::AssetCache& assetCache,
        Engine::AsyncWorkQueue& asyncWork,
        Engine::AsyncCompletedJob& completed)
    {
        if (Engine::AuthoredSceneCacheReadJobResult* cacheRead =
                std::any_cast<Engine::AuthoredSceneCacheReadJobResult>(&completed.result)) {
            ++runtime.asyncJobsCompleted;
            runtime.cacheReadMs = cacheRead->readMs;
            if (cacheRead->result.status == Engine::AuthoredSceneCacheStatus::Hit && cacheRead->result.payload) {
                commitAuthoredPayload(runtime, assetCache, std::move(*cacheRead->result.payload), true);
                return;
            }
            if (cacheRead->result.status == Engine::AuthoredSceneCacheStatus::Cancelled) {
                failAuthoredAsyncRuntime(runtime, "Authored scene cache read cancelled.");
                return;
            }
            enqueueAuthoredImport(runtime, asyncWork);
            return;
        }

        if (Engine::AuthoredSceneImportJobResult* import =
                std::any_cast<Engine::AuthoredSceneImportJobResult>(&completed.result)) {
            ++runtime.asyncJobsCompleted;
            runtime.importMs = import->importMs;
            if (import->cancelled) {
                failAuthoredAsyncRuntime(runtime, import->message);
                return;
            }
            if (!import->success) {
                failAuthoredAsyncRuntime(runtime, "Failed to import authored scene asynchronously: " + import->message);
                return;
            }

            if (runtime.settings.cache.policy == Engine::AuthoredSceneCachePolicy::GenerateOnMiss ||
                runtime.settings.cache.policy == Engine::AuthoredSceneCachePolicy::Refresh) {
                runtime.cacheWriteJob = Engine::enqueueAuthoredSceneCacheWrite(
                    asyncWork,
                    runtime.cacheManifest,
                    import->payload);
                if (runtime.cacheWriteJob.id != UINT64_MAX) {
                    runtime.asyncPhase = AuthoredRuntime::AsyncPhase::CacheWritePending;
                    ++runtime.asyncJobsQueued;
                }
            }

            commitAuthoredPayload(runtime, assetCache, std::move(import->payload), false);
            return;
        }

        if (Engine::AuthoredSceneCacheWriteJobResult* cacheWrite =
                std::any_cast<Engine::AuthoredSceneCacheWriteJobResult>(&completed.result)) {
            ++runtime.asyncJobsCompleted;
            runtime.cacheWriteMs = cacheWrite->writeMs;
            if (cacheWrite->result.status != Engine::AuthoredSceneCacheStatus::WriteSuccess &&
                cacheWrite->result.status != Engine::AuthoredSceneCacheStatus::Cancelled) {
                ++runtime.asyncJobsFailed;
                SDL_Log("Authored scene cache write failed: %s", cacheWrite->result.message.c_str());
            }
            if (runtime.usingStreaming) {
                runtime.asyncPhase = AuthoredRuntime::AsyncPhase::Committed;
                updateAuthoredAsyncDiagnostics(runtime, asyncWork.pendingCount(), cacheWrite->result.message);
            }
        }
    }
