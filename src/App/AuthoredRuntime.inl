    enum class AppSceneMode {
        Procedural,
        Authored,
    };

    constexpr const char* DefaultSponzaScenePath = "assets/main_sponza/NewSponza_Main_glTF_003.gltf";
    constexpr const char* DemoAuthoredSceneFixturePath = "tests/assets/fixtures/authored_scene_fixture.gltf";
    constexpr const char* DemoAnimatedModelFixturePath = "tests/assets/fixtures/skinned_animation_fixture.gltf";

    struct AppSceneSelection {
        AppSceneMode mode = BuildDebugToolsEnabled ? AppSceneMode::Procedural : AppSceneMode::Authored;
        std::filesystem::path authoredPath = DefaultSponzaScenePath;
        std::filesystem::path animatedModelPath = DemoAnimatedModelFixturePath;
        bool authoredPathExplicit = false;
        bool animatedModelPathExplicit = false;
    };

    std::string_view sceneModeName(AppSceneMode mode)
    {
        return mode == AppSceneMode::Authored ? "Authored" : "Procedural";
    }

    AppSceneSelection parseSceneSelection(int argc, char** argv)
    {
        AppSceneSelection selection;
        for (int index = 1; index < argc; ++index) {
            const std::string_view argument = argv[index] ? std::string_view{argv[index]} : std::string_view{};
            if (argument == "--scene" && index + 1 < argc) {
                const std::string_view value = argv[++index] ? std::string_view{argv[index]} : std::string_view{};
                if (value == "authored") {
                    selection.mode = AppSceneMode::Authored;
                } else if (value == "procedural") {
                    selection.mode = AppSceneMode::Procedural;
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
        AuthoredFallbackScene fallback;
        bool usingFallback = false;
        bool usingStreaming = false;
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

        void shutdown()
        {
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
        settings.hasAnimationDiagnostics = true;
        settings.animationLoaded = runtime.loaded;
        settings.animationEnabled = runtime.enabled;
        settings.animationPlaying = runtime.playback.playing;
        settings.animationLooping = runtime.playback.loop;
        settings.animationPath = runtime.sourcePath.generic_string();
        settings.animationStatus = runtime.status;
        settings.animationAsyncPhase = std::string{animatedAsyncPhaseName(runtime.asyncPhase)};
        settings.animationAsyncMessage = runtime.asyncMessage;
        settings.animationCacheStatus = Engine::animatedModelCacheStatusName(runtime.cacheStatus);
        settings.animationCacheIdentity = runtime.cacheManifest.identityHash;
        settings.animationCacheMessage = runtime.loaded ? runtime.model.diagnostics().cacheMessage : runtime.asyncMessage;
        settings.animationAsyncQueued = runtime.asyncJobsQueued;
        settings.animationAsyncCompleted = runtime.asyncJobsCompleted;
        settings.animationAsyncFailed = runtime.asyncJobsFailed;
        settings.animationAsyncPending = runtime.asyncJobsQueued > runtime.asyncJobsCompleted
            ? runtime.asyncJobsQueued - runtime.asyncJobsCompleted
            : 0;
        settings.animationCacheReadMs = runtime.cacheReadMs;
        settings.animationImportMs = runtime.importMs;
        settings.animationCacheWriteMs = runtime.cacheWriteMs;
        settings.animationClipCount = runtime.loaded ? runtime.model.clipCount() : 0;
        settings.animationClipIndex = runtime.crossfade.active
            ? runtime.crossfade.targetClipIndex
            : runtime.playback.clipIndex;
        settings.animationJointCount = runtime.loaded ? runtime.model.jointCount() : 0;
        settings.animationSkinnedInstanceCount = runtime.loaded ? runtime.model.skinnedInstanceCount() : 0;
        settings.animationCreatedSkinnedMeshCount = runtime.loaded
            ? runtime.model.diagnostics().createdSkinnedMeshCount
            : 0;
        settings.animationTextureFallbackCount = runtime.loaded
            ? runtime.model.diagnostics().fallbackTextureCount
            : 0;
        settings.animationWarningCount = runtime.loaded
            ? static_cast<uint32_t>(runtime.model.diagnostics().warnings.size())
            : 0;
        settings.animationLastWarning = runtime.loaded && !runtime.model.diagnostics().warnings.empty()
            ? runtime.model.diagnostics().warnings.back()
            : std::string{};
        settings.animationSampledFrameCount = runtime.sampledFrameCount;
        settings.animationFailedPoseUpdateCount = runtime.failedPoseUpdateCount;
        settings.animationCompletedCrossfadeCount = runtime.completedCrossfadeCount;
        settings.animationTimeSeconds = runtime.playback.timeSeconds;
        settings.animationClipDurationSeconds = runtime.loaded
            ? runtime.model.clipDuration(settings.animationClipIndex)
            : 0.0f;
        settings.animationPlaybackSpeed = runtime.playback.speed;
        settings.animationCrossfadeActive = runtime.crossfade.active;
        settings.animationCrossfadeTargetClipIndex = runtime.crossfade.targetClipIndex;
        settings.animationCrossfadeElapsedSeconds = runtime.crossfade.elapsedSeconds;
        settings.animationCrossfadeDurationSeconds = runtime.crossfade.durationSeconds;
        settings.animationCrossfadeWeight = runtime.crossfade.durationSeconds > 0.0f
            ? std::clamp(runtime.crossfade.elapsedSeconds / runtime.crossfade.durationSeconds, 0.0f, 1.0f)
            : 0.0f;
    }

    void applyAnimatedDebugSettings(
        AnimatedSampleRuntime& runtime,
        const Renderer::DebugUi::RendererDebugSettings& settings)
    {
        if (!runtime.loaded || !settings.hasAnimationDiagnostics) {
            return;
        }

        runtime.enabled = settings.animationEnabled && runtime.model.skinnedInstanceCount() > 0 && runtime.model.clipCount() > 0;
        runtime.playback.playing = settings.animationPlaying;
        runtime.playback.loop = settings.animationLooping;
        runtime.playback.speed = settings.animationPlaybackSpeed;
        if (runtime.model.clipCount() > 0) {
            const uint32_t requestedClip = std::min(settings.animationClipIndex, runtime.model.clipCount() - 1);
            if (requestedClip != runtime.playback.clipIndex &&
                (!runtime.crossfade.active || requestedClip != runtime.crossfade.targetClipIndex)) {
                runtime.crossfade = Engine::beginCrossfade(
                    runtime.playback,
                    requestedClip,
                    Engine::DefaultAnimationCrossfadeSeconds);
            }
            const float duration = runtime.model.clipDuration(runtime.playback.clipIndex);
            runtime.playback.timeSeconds = std::clamp(settings.animationTimeSeconds, 0.0f, std::max(duration, 0.0f));
            if (runtime.crossfade.active) {
                runtime.crossfade.source.playing = runtime.playback.playing;
                runtime.crossfade.source.loop = runtime.playback.loop;
                runtime.crossfade.source.speed = runtime.playback.speed;
                runtime.crossfade.source.timeSeconds = runtime.playback.timeSeconds;
                runtime.crossfade.target.playing = runtime.playback.playing;
                runtime.crossfade.target.loop = runtime.playback.loop;
                runtime.crossfade.target.speed = runtime.playback.speed;
            }
        }
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
        const Engine::AuthoredSceneDiagnosticsSummary summary =
            Engine::summarizeAuthoredSceneDiagnostics(diagnostics);
        settings.hasAuthoredSceneDiagnostics = true;
        settings.authoredScenePath = path.generic_string();
        settings.authoredSourceFormat = summary.sourceFormatName;
        settings.authoredCacheStatus = Engine::cacheStatusName(summary.cacheStatus);
        settings.authoredCacheMessage = summary.cacheMessage;
        settings.authoredAsyncPhase = summary.asyncPhase.empty() ? "n/a" : summary.asyncPhase;
        settings.authoredAsyncMessage = summary.asyncMessage;
        settings.authoredImportedNodes = summary.importedNodeCount;
        settings.authoredImportedMeshes = summary.importedMeshCount;
        settings.authoredImportedPrimitives = summary.importedPrimitiveCount;
        settings.authoredImportedMaterials = summary.importedMaterialCount;
        settings.authoredImportedTextures = summary.importedTextureCount;
        settings.authoredImportedLights = summary.importedLightCount;
        settings.authoredCreatedMeshes = summary.createdMeshCount;
        settings.authoredCreatedMaterials = summary.createdMaterialCount;
        settings.authoredCreatedInstances = summary.createdInstanceCount;
        settings.authoredCreatedLights = summary.createdLightCount;
        settings.authoredTextureLoaded = summary.textureLoadSuccessCount;
        settings.authoredTextureFailed = summary.textureLoadFailureCount;
        settings.authoredTextureFallback = summary.fallbackTextureCount;
        settings.authoredTextureBytes = summary.textureEstimatedBytes;
        settings.authoredTotalSectors = summary.totalSectorCount;
        settings.authoredLoadedSectors = summary.loadedSectorCount;
        settings.authoredPendingLoadSectors = summary.pendingLoadSectorCount;
        settings.authoredPendingUnloadSectors = summary.pendingUnloadSectorCount;
        settings.authoredFailedSectors = summary.failedSectorCount;
        settings.authoredSectorBytes = summary.sectorEstimatedBytes;
        settings.authoredActiveLights = summary.activeAuthoredLightCount;
        settings.authoredDisabledZeroLights = summary.disabledZeroIntensityLightCount;
        settings.authoredSkippedOverBudgetLights = summary.skippedOverBudgetLightCount;
        settings.authoredWarnings = summary.warningCount;
        settings.authoredCacheReadMs = summary.asyncCacheReadMs;
        settings.authoredImportMs = summary.asyncImportMs;
        settings.authoredCacheWriteMs = summary.asyncCacheWriteMs;
        settings.authoredAsyncQueued = summary.asyncJobsQueued;
        settings.authoredAsyncCompleted = summary.asyncJobsCompleted;
        settings.authoredAsyncFailed = summary.asyncJobsFailed;
        settings.authoredAsyncPending = summary.asyncPendingJobs;
    }

    Renderer::Aabb authoredBoundsOrFallback(const Engine::AuthoredSceneBounds& bounds)
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
        Engine::AsyncWorkQueue& asyncWork)
    {
        AuthoredRuntime runtime;
        const std::filesystem::path resolvedPath = resolveAuthoredScenePath(path);
        if (std::filesystem::exists(resolvedPath)) {
            runtime.sourcePath = resolvedPath;
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

