cmake_minimum_required(VERSION 3.31)

project(ManualEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL3 CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(bgfx CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(Jolt CONFIG REQUIRED)
find_package(Lua REQUIRED)
find_package(recastnavigation CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)

enable_testing()

add_executable(manual_engine
    src/App/main.cpp
    src/App/EditorProjectSettings.cpp
    src/App/EditorSettingsReflection.cpp
    src/App/ModernSceneLoop.cpp
    src/Assets/Assimp/Importer.cpp
    src/Assets/HeightmapImporter.cpp
   src/Engine/AnimatedModelPose.cpp
    src/Engine/ActorAuthoring.cpp
    src/Engine/ActorComponentAuthoring.cpp
   src/Engine/AsyncWorkQueue.cpp
    src/Engine/AssetCache.cpp
    src/Engine/AssetRegistry.cpp
    src/Engine/Biome.cpp
    src/Engine/ChunkTypes.cpp
    src/Engine/CursorTrace.cpp
    src/Engine/DebugVisualization.cpp
    src/Engine/EventQueue.cpp
    src/Engine/FixedStepLoop.cpp
    src/Engine/FrameBudget.cpp
    src/Engine/InputMapping.cpp
    src/Engine/ImportedSceneResources.cpp
   src/Engine/NavigationConnectivity.cpp
    src/Engine/NavigationCache.cpp
    src/Engine/NavigationCacheAsync.cpp
    src/Engine/NavigationProfile.cpp
    src/Engine/NavigationRuntime.cpp
    src/Engine/NavigationSceneGeometry.cpp
   src/Engine/Navigation.cpp
    src/Engine/OpenWorldStreaming.cpp
    src/Engine/OpenWorldStreamingAssets.cpp
    src/Engine/OpenWorldStreamingBake.cpp
    src/Engine/OpenWorldStreamingRuntime.cpp
    src/Engine/OpenWorldStreamingSceneChunks.cpp
    src/Engine/OrbitCamera.cpp
   src/Engine/Physics/ScenePhysics.cpp
    src/Engine/Reflection.cpp
    src/Engine/SceneBehaviorHooks.cpp
    src/Engine/SceneCharacterMovement.cpp
    src/Engine/SceneLuaScripting.cpp
    src/Engine/SceneReflection.cpp
    src/Engine/Scene/RendererSceneRenderBackend.cpp
    src/Engine/Scene/AnimatedSceneAdapter.cpp
    src/Engine/Scene/AuthoredSceneAdapter.cpp
    src/Engine/Scene/Scene.cpp
    src/Engine/Scene/SceneRenderBridge.cpp
    src/Engine/SceneSerialization.cpp
   src/Engine/TerrainDataset.cpp
   src/Engine/TerrainDerivedCache.cpp
    src/Engine/TerrainDerivedCacheAsync.cpp
    src/Engine/TerrainImport.cpp
    src/Engine/TerrainMaterialMetadata.cpp
    src/Engine/TerrainMaterialRenderAdapter.cpp
    src/Engine/TerrainNavigationAdapter.cpp
    src/Engine/TerrainPhysicsColliderAdapter.cpp
    src/Engine/TerrainRenderLodAdapter.cpp
    src/Engine/TerrainSampleMaterials.cpp
    src/Engine/TerrainSerializationPrep.cpp
   src/Engine/input.cpp
    src/Renderer/DebugUi.cpp
    src/Renderer/Scene.cpp
    src/Renderer/Texture.cpp
    src/Renderer/core.cpp
    src/Renderer/VertexLayouts.cpp
)

target_include_directories(manual_engine
    PRIVATE
        src
        ${Stb_INCLUDE_DIR}
        ${LUA_INCLUDE_DIR}
)

target_compile_definitions(manual_engine
    PRIVATE
        MANUAL_ENGINE_ENABLE_DEBUG_TOOLS=$<IF:$<CONFIG:Debug>,1,0>
)

target_link_libraries(manual_engine
    PRIVATE
        SDL3::SDL3
        assimp::assimp
        bgfx::bgfx
        glm::glm
        $<$<CONFIG:Debug>:imgui::imgui>
        Jolt::Jolt
        ${LUA_LIBRARIES}
        RecastNavigation::Recast
        RecastNavigation::Detour
        RecastNavigation::DetourTileCache
        RecastNavigation::DetourCrowd
        RecastNavigation::DebugUtils
        yaml-cpp::yaml-cpp
)

get_target_property(MANUAL_ENGINE_APP_SOURCES manual_engine SOURCES)
list(REMOVE_ITEM MANUAL_ENGINE_APP_SOURCES src/App/main.cpp)

add_executable(manual_engine_editor
    src/App/editor_main.cpp
    src/App/EditorRebuildCoordinator.cpp
    src/App/EditorToolScripting.cpp
    src/App/EditorUi.cpp
    ${MANUAL_ENGINE_APP_SOURCES}
)

target_include_directories(manual_engine_editor
    PRIVATE
        src
        ${Stb_INCLUDE_DIR}
        ${LUA_INCLUDE_DIR}
)

target_compile_definitions(manual_engine_editor
    PRIVATE
        MANUAL_ENGINE_ENABLE_DEBUG_TOOLS=$<IF:$<CONFIG:Debug>,1,0>
)

target_link_libraries(manual_engine_editor
    PRIVATE
        SDL3::SDL3
        assimp::assimp
        bgfx::bgfx
        glm::glm
        $<$<CONFIG:Debug>:imgui::imgui>
        Jolt::Jolt
        ${LUA_LIBRARIES}
        RecastNavigation::Recast
        RecastNavigation::Detour
        RecastNavigation::DetourTileCache
        RecastNavigation::DetourCrowd
        RecastNavigation::DebugUtils
        yaml-cpp::yaml-cpp
)


add_executable(manual_engine_navigation_runtime_tests
    tests/navigation/NavigationRuntimeTestRunner.cpp
    tests/navigation/RendererStubs.cpp
    src/Engine/Biome.cpp
    src/Engine/ChunkTypes.cpp
    src/Engine/CursorTrace.cpp
    src/Engine/Navigation.cpp
    src/Engine/NavigationConnectivity.cpp
    src/Engine/NavigationRuntime.cpp
    src/Engine/Scene/Scene.cpp
)

target_include_directories(manual_engine_navigation_runtime_tests
    PRIVATE
        src
)

target_link_libraries(manual_engine_navigation_runtime_tests
    PRIVATE
        glm::glm
        RecastNavigation::Recast
        RecastNavigation::Detour
        yaml-cpp::yaml-cpp
)

add_test(NAME manual_engine_navigation_runtime_tests COMMAND manual_engine_navigation_runtime_tests)

add_executable(manual_engine_navigation_scene_geometry_tests
    tests/navigation/NavigationSceneGeometryTestRunner.cpp
    tests/authored_scene/RendererOwnershipStubs.cpp
    src/Assets/Assimp/Importer.cpp
    src/Engine/AssetCache.cpp
    src/Engine/ChunkTypes.cpp
    src/Engine/ImportedSceneResources.cpp
    src/Engine/Navigation.cpp
    src/Engine/NavigationConnectivity.cpp
    src/Engine/NavigationRuntime.cpp
    src/Engine/NavigationSceneGeometry.cpp
    src/Engine/Scene/AuthoredSceneAdapter.cpp
    src/Engine/Scene/Scene.cpp
    src/Engine/Scene/SceneRenderBridge.cpp
)

target_include_directories(manual_engine_navigation_scene_geometry_tests
    PRIVATE
        src
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
)

target_link_libraries(manual_engine_navigation_scene_geometry_tests
    PRIVATE
        assimp::assimp
        glm::glm
        RecastNavigation::Recast
        RecastNavigation::Detour
)

add_test(NAME manual_engine_navigation_scene_geometry_tests COMMAND manual_engine_navigation_scene_geometry_tests)

add_executable(manual_engine_asset_import_tests
    tests/assets/AssetImportTestRunner.cpp
    src/Assets/Assimp/Importer.cpp
)

target_include_directories(manual_engine_asset_import_tests
    PRIVATE
        src
)

target_link_libraries(manual_engine_asset_import_tests
    PRIVATE
        assimp::assimp
        glm::glm
)

add_test(NAME manual_engine_asset_import_tests COMMAND manual_engine_asset_import_tests)

add_executable(manual_engine_heightmap_import_tests
    tests/assets/HeightmapImportTestRunner.cpp
    src/Assets/Assimp/Importer.cpp
    src/Assets/HeightmapImporter.cpp
    src/Engine/AssetRegistry.cpp
    src/Engine/TerrainImport.cpp
)

target_include_directories(manual_engine_heightmap_import_tests
    PRIVATE
        src
        ${Stb_INCLUDE_DIR}
)

target_link_libraries(manual_engine_heightmap_import_tests
    PRIVATE
        assimp::assimp
        glm::glm
)

add_test(NAME manual_engine_heightmap_import_tests COMMAND manual_engine_heightmap_import_tests)

add_executable(manual_engine_debug_visualization_tests
    tests/engine/DebugVisualizationTestRunner.cpp
    src/Engine/DebugVisualization.cpp
)

target_include_directories(manual_engine_debug_visualization_tests
    PRIVATE
        src
)

target_compile_definitions(manual_engine_debug_visualization_tests
    PRIVATE
        MANUAL_ENGINE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(manual_engine_debug_visualization_tests
    PRIVATE
        glm::glm
)

add_test(NAME manual_engine_debug_visualization_tests COMMAND manual_engine_debug_visualization_tests)

add_executable(manual_engine_editor_project_settings_tests
    tests/app/EditorProjectSettingsTestRunner.cpp
    tests/app/EditorProjectSettingsStreamingStub.cpp
    src/App/EditorProjectSettings.cpp
)

target_include_directories(manual_engine_editor_project_settings_tests
    PRIVATE
        src
        ${Stb_INCLUDE_DIR}
        ${LUA_INCLUDE_DIR}
)

target_link_libraries(manual_engine_editor_project_settings_tests
    PRIVATE
        SDL3::SDL3
        bgfx::bgfx
        glm::glm
        imgui::imgui
        yaml-cpp::yaml-cpp
)

add_test(NAME manual_engine_editor_project_settings_tests COMMAND manual_engine_editor_project_settings_tests)
set_tests_properties(manual_engine_editor_project_settings_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

add_executable(manual_engine_editor_settings_reflection_tests
    tests/app/EditorSettingsReflectionTestRunner.cpp
    tests/app/EditorProjectSettingsStreamingStub.cpp
    src/App/EditorProjectSettings.cpp
    src/App/EditorSettingsReflection.cpp
    src/Engine/Reflection.cpp
)

target_include_directories(manual_engine_editor_settings_reflection_tests
    PRIVATE
        src
        ${Stb_INCLUDE_DIR}
        ${LUA_INCLUDE_DIR}
)

target_link_libraries(manual_engine_editor_settings_reflection_tests
    PRIVATE
        SDL3::SDL3
        bgfx::bgfx
        glm::glm
        imgui::imgui
        yaml-cpp::yaml-cpp
)

add_test(NAME manual_engine_editor_settings_reflection_tests COMMAND manual_engine_editor_settings_reflection_tests)
set_tests_properties(manual_engine_editor_settings_reflection_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

add_executable(manual_engine_editor_ui_tests
    tests/app/EditorUiTestRunner.cpp
    src/Engine/ActorAuthoring.cpp
    src/Engine/ActorComponentAuthoring.cpp
    src/App/EditorProjectSettings.cpp
    src/App/EditorRebuildCoordinator.cpp
    src/App/EditorSettingsReflection.cpp
    src/App/EditorToolScripting.cpp
    src/App/EditorUi.cpp
    src/Assets/HeightmapImporter.cpp
    src/Engine/AsyncWorkQueue.cpp
    src/Engine/Biome.cpp
    src/Engine/ChunkTypes.cpp
    src/Engine/FrameBudget.cpp
    src/Engine/Navigation.cpp
    src/Engine/NavigationCache.cpp
    src/Engine/OpenWorldStreaming.cpp
    src/Engine/OpenWorldStreamingBake.cpp
    src/Engine/OpenWorldStreamingRuntime.cpp
    src/Engine/Physics/ScenePhysics.cpp
    src/Engine/Reflection.cpp
    src/Engine/Scene/Scene.cpp
    src/Engine/SceneSerialization.cpp
    src/Engine/TerrainDataset.cpp
    src/Engine/TerrainDerivedCache.cpp
    src/Engine/TerrainImport.cpp
    src/Engine/TerrainNavigationAdapter.cpp
    src/Engine/TerrainPhysicsColliderAdapter.cpp
    src/Engine/TerrainRenderLodAdapter.cpp
    src/Engine/TerrainSerializationPrep.cpp
)

target_include_directories(manual_engine_editor_ui_tests
    PRIVATE
        src
        ${Stb_INCLUDE_DIR}
        ${LUA_INCLUDE_DIR}
)

target_compile_definitions(manual_engine_editor_ui_tests
    PRIVATE
        MANUAL_ENGINE_ENABLE_DEBUG_TOOLS=0
)

target_link_libraries(manual_engine_editor_ui_tests
    PRIVATE
        SDL3::SDL3
        bgfx::bgfx
        glm::glm
        Jolt::Jolt
        ${LUA_LIBRARIES}
        RecastNavigation::Recast
        RecastNavigation::Detour
        RecastNavigation::DetourTileCache
        RecastNavigation::DetourCrowd
        RecastNavigation::DebugUtils
        yaml-cpp::yaml-cpp
)

add_test(NAME manual_engine_editor_ui_tests COMMAND manual_engine_editor_ui_tests)
set_tests_properties(manual_engine_editor_ui_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

add_executable(manual_engine_editor_rebuild_coordinator_tests
    tests/app/EditorRebuildCoordinatorTestRunner.cpp
    src/Engine/ActorAuthoring.cpp
    src/Engine/ActorComponentAuthoring.cpp
    src/App/EditorProjectSettings.cpp
    src/App/EditorRebuildCoordinator.cpp
    src/App/EditorSettingsReflection.cpp
    src/App/EditorToolScripting.cpp
    src/App/EditorUi.cpp
    src/Assets/HeightmapImporter.cpp
    src/Engine/AsyncWorkQueue.cpp
    src/Engine/Biome.cpp
    src/Engine/ChunkTypes.cpp
    src/Engine/FrameBudget.cpp
    src/Engine/Navigation.cpp
    src/Engine/NavigationCache.cpp
    src/Engine/OpenWorldStreaming.cpp
    src/Engine/OpenWorldStreamingBake.cpp
    src/Engine/OpenWorldStreamingRuntime.cpp
    src/Engine/Physics/ScenePhysics.cpp
    src/Engine/Reflection.cpp
    src/Engine/Scene/Scene.cpp
    src/Engine/SceneSerialization.cpp
    src/Engine/TerrainDataset.cpp
    src/Engine/TerrainDerivedCache.cpp
    src/Engine/TerrainImport.cpp
    src/Engine/TerrainNavigationAdapter.cpp
    src/Engine/TerrainPhysicsColliderAdapter.cpp
    src/Engine/TerrainRenderLodAdapter.cpp
    src/Engine/TerrainSerializationPrep.cpp
)

target_include_directories(manual_engine_editor_rebuild_coordinator_tests
    PRIVATE
        src
        ${Stb_INCLUDE_DIR}
        ${LUA_INCLUDE_DIR}
)

target_compile_definitions(manual_engine_editor_rebuild_coordinator_tests
    PRIVATE
        MANUAL_ENGINE_ENABLE_DEBUG_TOOLS=0
)

target_link_libraries(manual_engine_editor_rebuild_coordinator_tests
    PRIVATE
        SDL3::SDL3
        bgfx::bgfx
        glm::glm
        Jolt::Jolt
        ${LUA_LIBRARIES}
        RecastNavigation::Recast
        RecastNavigation::Detour
        RecastNavigation::DetourTileCache
        RecastNavigation::DetourCrowd
        RecastNavigation::DebugUtils
        yaml-cpp::yaml-cpp
)

add_test(NAME manual_engine_editor_rebuild_coordinator_tests COMMAND manual_engine_editor_rebuild_coordinator_tests)
set_tests_properties(manual_engine_editor_rebuild_coordinator_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

add_executable(manual_engine_editor_live_apply_tests
    tests/app/EditorLiveApplyTestRunner.cpp
    src/Engine/ActorAuthoring.cpp
    src/Engine/ActorComponentAuthoring.cpp
    src/App/EditorProjectSettings.cpp
    src/App/EditorRebuildCoordinator.cpp
    src/App/EditorSettingsReflection.cpp
    src/App/EditorToolScripting.cpp
    src/App/EditorUi.cpp
    src/Assets/HeightmapImporter.cpp
    src/Engine/AsyncWorkQueue.cpp
    src/Engine/Biome.cpp
    src/Engine/ChunkTypes.cpp
    src/Engine/FrameBudget.cpp
    src/Engine/Navigation.cpp
    src/Engine/NavigationCache.cpp
    src/Engine/OpenWorldStreaming.cpp
    src/Engine/OpenWorldStreamingBake.cpp
    src/Engine/OpenWorldStreamingRuntime.cpp
    src/Engine/Physics/ScenePhysics.cpp
    src/Engine/Reflection.cpp
    src/Engine/Scene/Scene.cpp
    src/Engine/SceneSerialization.cpp
    src/Engine/TerrainDataset.cpp
    src/Engine/TerrainDerivedCache.cpp
    src/Engine/TerrainImport.cpp
    src/Engine/TerrainNavigationAdapter.cpp
    src/Engine/TerrainPhysicsColliderAdapter.cpp
    src/Engine/TerrainRenderLodAdapter.cpp
    src/Engine/TerrainSerializationPrep.cpp
)

target_include_directories(manual_engine_editor_live_apply_tests
    PRIVATE
        src
        ${Stb_INCLUDE_DIR}
        ${LUA_INCLUDE_DIR}
)

target_compile_definitions(manual_engine_editor_live_apply_tests
    PRIVATE
        MANUAL_ENGINE_ENABLE_DEBUG_TOOLS=0
        MANUAL_ENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(manual_engine_editor_live_apply_tests
    PRIVATE
        SDL3::SDL3
        bgfx::bgfx
        glm::glm
        Jolt::Jolt
        ${LUA_LIBRARIES}
        RecastNavigation::Recast
        RecastNavigation::Detour
        RecastNavigation::DetourTileCache
        RecastNavigation::DetourCrowd
        RecastNavigation::DebugUtils
        yaml-cpp::yaml-cpp
)

add_test(NAME manual_engine_editor_live_apply_tests COMMAND manual_engine_editor_live_apply_tests)
set_tests_properties(manual_engine_editor_live_apply_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

add_executable(manual_engine_editor_tool_scripting_tests
    tests/app/EditorToolScriptingTestRunner.cpp
    src/Engine/ActorAuthoring.cpp
    src/Engine/ActorComponentAuthoring.cpp
    src/App/EditorProjectSettings.cpp
    src/App/EditorRebuildCoordinator.cpp
    src/App/EditorSettingsReflection.cpp
    src/App/EditorToolScripting.cpp
    src/App/EditorUi.cpp
    src/Assets/HeightmapImporter.cpp
    src/Engine/AsyncWorkQueue.cpp
    src/Engine/Biome.cpp
    src/Engine/ChunkTypes.cpp
    src/Engine/FrameBudget.cpp
    src/Engine/Navigation.cpp
    src/Engine/NavigationCache.cpp
    src/Engine/OpenWorldStreaming.cpp
    src/Engine/OpenWorldStreamingBake.cpp
    src/Engine/OpenWorldStreamingRuntime.cpp
    src/Engine/Physics/ScenePhysics.cpp
    src/Engine/Reflection.cpp
    src/Engine/Scene/Scene.cpp
    src/Engine/SceneSerialization.cpp
    src/Engine/TerrainDataset.cpp
    src/Engine/TerrainDerivedCache.cpp
    src/Engine/TerrainImport.cpp
    src/Engine/TerrainNavigationAdapter.cpp
    src/Engine/TerrainPhysicsColliderAdapter.cpp
    src/Engine/TerrainRenderLodAdapter.cpp
    src/Engine/TerrainSerializationPrep.cpp
)

target_include_directories(manual_engine_editor_tool_scripting_tests
    PRIVATE
        src
        ${Stb_INCLUDE_DIR}
        ${LUA_INCLUDE_DIR}
)

target_compile_definitions(manual_engine_editor_tool_scripting_tests
    PRIVATE
        MANUAL_ENGINE_ENABLE_DEBUG_TOOLS=0
        MANUAL_ENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(manual_engine_editor_tool_scripting_tests
    PRIVATE
        SDL3::SDL3
        bgfx::bgfx
        glm::glm
        Jolt::Jolt
        ${LUA_LIBRARIES}
        RecastNavigation::Recast
        RecastNavigation::Detour
        RecastNavigation::DetourTileCache
        RecastNavigation::DetourCrowd
        RecastNavigation::DebugUtils
        yaml-cpp::yaml-cpp
)

add_test(NAME manual_engine_editor_tool_scripting_tests COMMAND manual_engine_editor_tool_scripting_tests)
set_tests_properties(manual_engine_editor_tool_scripting_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

add_executable(manual_engine_open_world_streaming_tests
    tests/engine/OpenWorldStreamingTestRunner.cpp
    tests/authored_scene/RendererOwnershipStubs.cpp
    src/Assets/Assimp/Importer.cpp
    src/Assets/HeightmapImporter.cpp
    src/Engine/ActorAuthoring.cpp
    src/Engine/ActorComponentAuthoring.cpp
    src/Engine/AsyncWorkQueue.cpp
    src/Engine/AssetCache.cpp
    src/Engine/AssetRegistry.cpp
    src/Engine/Biome.cpp
    src/Engine/ChunkTypes.cpp
    src/Engine/FrameBudget.cpp
    src/Engine/Navigation.cpp
    src/Engine/NavigationCache.cpp
    src/Engine/OpenWorldStreaming.cpp
    src/Engine/OpenWorldStreamingAssets.cpp
    src/Engine/OpenWorldStreamingBake.cpp
    src/Engine/OpenWorldStreamingSceneChunks.cpp
    src/Engine/Physics/ScenePhysics.cpp
    src/Engine/Reflection.cpp
    src/Engine/Scene/Scene.cpp
    src/Engine/SceneSerialization.cpp
   src/Engine/TerrainDataset.cpp
    src/Engine/TerrainDerivedCache.cpp
    src/Engine/TerrainImport.cpp
    src/Engine/TerrainNavigationAdapter.cpp
    src/Engine/TerrainPhysicsColliderAdapter.cpp
    src/Engine/TerrainSerializationPrep.cpp
)

target_include_directories(manual_engine_open_world_streaming_tests
    PRIVATE
        src
        ${Stb_INCLUDE_DIR}
)

target_compile_definitions(manual_engine_open_world_streaming_tests
    PRIVATE
        MANUAL_ENGINE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(manual_engine_open_world_streaming_tests
    PRIVATE
        glm::glm
        assimp::assimp
        Jolt::Jolt
        RecastNavigation::Recast
        RecastNavigation::Detour
        RecastNavigation::DetourTileCache
        RecastNavigation::DetourCrowd
        RecastNavigation::DebugUtils
        yaml-cpp::yaml-cpp
)

add_test(NAME manual_engine_open_world_streaming_tests COMMAND manual_engine_open_world_streaming_tests)

add_executable(manual_engine_open_world_streaming_runtime_tests
    tests/engine/OpenWorldStreamingRuntimeTestRunner.cpp
    tests/navigation/RendererStubs.cpp
    src/Assets/Assimp/Importer.cpp
    src/Assets/HeightmapImporter.cpp
    src/Engine/ActorAuthoring.cpp
    src/Engine/ActorComponentAuthoring.cpp
    src/Engine/AsyncWorkQueue.cpp
    src/Engine/AssetRegistry.cpp
    src/Engine/Biome.cpp
    src/Engine/ChunkTypes.cpp
    src/Engine/FrameBudget.cpp
    src/Engine/Navigation.cpp
    src/Engine/NavigationCache.cpp
    src/Engine/OpenWorldStreaming.cpp
    src/Engine/OpenWorldStreamingBake.cpp
    src/Engine/OpenWorldStreamingRuntime.cpp
    src/Engine/Physics/ScenePhysics.cpp
    src/Engine/Reflection.cpp
    src/Engine/Scene/Scene.cpp
    src/Engine/SceneSerialization.cpp
   src/Engine/TerrainDataset.cpp
    src/Engine/TerrainDerivedCache.cpp
    src/Engine/TerrainImport.cpp
    src/Engine/TerrainNavigationAdapter.cpp
    src/Engine/TerrainPhysicsColliderAdapter.cpp
    src/Engine/TerrainSerializationPrep.cpp
)

target_include_directories(manual_engine_open_world_streaming_runtime_tests
    PRIVATE
        src
        ${Stb_INCLUDE_DIR}
)

target_compile_definitions(manual_engine_open_world_streaming_runtime_tests
    PRIVATE
        MANUAL_ENGINE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(manual_engine_open_world_streaming_runtime_tests
    PRIVATE
        glm::glm
        assimp::assimp
        Jolt::Jolt
        RecastNavigation::Recast
        RecastNavigation::Detour
        RecastNavigation::DetourTileCache
        RecastNavigation::DetourCrowd
        RecastNavigation::DebugUtils
        yaml-cpp::yaml-cpp
)

add_test(NAME manual_engine_open_world_streaming_runtime_tests COMMAND manual_engine_open_world_streaming_runtime_tests)

add_executable(manual_engine_open_world_scene_chunk_streaming_tests
    tests/engine/OpenWorldSceneChunkStreamingTestRunner.cpp
    src/Engine/ActorAuthoring.cpp
    src/Engine/ActorComponentAuthoring.cpp
    src/Engine/AsyncWorkQueue.cpp
    src/Engine/ChunkTypes.cpp
    src/Engine/FrameBudget.cpp
    src/Engine/NavigationCache.cpp
    src/Engine/OpenWorldStreaming.cpp
    src/Engine/OpenWorldStreamingSceneChunks.cpp
    src/Engine/Reflection.cpp
    src/Engine/Scene/Scene.cpp
    src/Engine/SceneSerialization.cpp
    src/Engine/TerrainDerivedCache.cpp
    src/Engine/TerrainSerializationPrep.cpp
)

target_include_directories(manual_engine_open_world_scene_chunk_streaming_tests
    PRIVATE
        src
)

target_compile_definitions(manual_engine_open_world_scene_chunk_streaming_tests
    PRIVATE
        MANUAL_ENGINE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(manual_engine_open_world_scene_chunk_streaming_tests
    PRIVATE
        glm::glm
        yaml-cpp::yaml-cpp
)

add_test(NAME manual_engine_open_world_scene_chunk_streaming_tests COMMAND manual_engine_open_world_scene_chunk_streaming_tests)

add_executable(manual_engine_terrain_dataset_tests
    tests/terrain/TerrainDatasetTestRunner.cpp
    src/Engine/Biome.cpp
    src/Engine/TerrainDataset.cpp
)

target_include_directories(manual_engine_terrain_dataset_tests
    PRIVATE
        src
)

target_link_libraries(manual_engine_terrain_dataset_tests
    PRIVATE
        glm::glm
        yaml-cpp::yaml-cpp
)

add_test(NAME manual_engine_terrain_dataset_tests COMMAND manual_engine_terrain_dataset_tests)

add_executable(manual_engine_terrain_cache_tests
    tests/terrain/TerrainDerivedCacheTestRunner.cpp
    src/Engine/AsyncWorkQueue.cpp
    src/Engine/TerrainDerivedCache.cpp
    src/Engine/TerrainDerivedCacheAsync.cpp
)

target_include_directories(manual_engine_terrain_cache_tests
    PRIVATE
        src
)

target_link_libraries(manual_engine_terrain_cache_tests
    PRIVATE
        glm::glm
        yaml-cpp::yaml-cpp
)

add_test(NAME manual_engine_terrain_cache_tests COMMAND manual_engine_terrain_cache_tests)

add_executable(manual_engine_terrain_render_lod_adapter_tests
    tests/terrain/TerrainRenderLodAdapterTestRunner.cpp
    tests/navigation/RendererStubs.cpp
    src/Engine/Biome.cpp
   src/Engine/TerrainDataset.cpp
    src/Engine/TerrainDerivedCache.cpp
    src/Engine/TerrainRenderLodAdapter.cpp
)

target_include_directories(manual_engine_terrain_render_lod_adapter_tests
    PRIVATE
        src
)

target_link_libraries(manual_engine_terrain_render_lod_adapter_tests
    PRIVATE
        glm::glm
        yaml-cpp::yaml-cpp
)

add_test(NAME manual_engine_terrain_render_lod_adapter_tests COMMAND manual_engine_terrain_render_lod_adapter_tests)

add_executable(manual_engine_terrain_navigation_adapter_tests
    tests/terrain/TerrainNavigationAdapterTestRunner.cpp
    tests/navigation/RendererStubs.cpp
    src/Engine/Biome.cpp
    src/Engine/ChunkTypes.cpp
    src/Engine/Navigation.cpp
    src/Engine/NavigationCache.cpp
   src/Engine/TerrainDataset.cpp
    src/Engine/TerrainNavigationAdapter.cpp
)

target_include_directories(manual_engine_terrain_navigation_adapter_tests
    PRIVATE
        src
)

target_link_libraries(manual_engine_terrain_navigation_adapter_tests
    PRIVATE
        glm::glm
        RecastNavigation::Recast
        RecastNavigation::Detour
        yaml-cpp::yaml-cpp
)

add_test(NAME manual_engine_terrain_navigation_adapter_tests COMMAND manual_engine_terrain_navigation_adapter_tests)

add_executable(manual_engine_terrain_physics_collider_adapter_tests
    tests/terrain/TerrainPhysicsColliderAdapterTestRunner.cpp
    tests/navigation/RendererStubs.cpp
    src/Engine/Biome.cpp
    src/Engine/Physics/ScenePhysics.cpp
    src/Engine/Scene/Scene.cpp
   src/Engine/TerrainDataset.cpp
    src/Engine/TerrainPhysicsColliderAdapter.cpp
)

target_include_directories(manual_engine_terrain_physics_collider_adapter_tests
    PRIVATE
        src
)

target_link_libraries(manual_engine_terrain_physics_collider_adapter_tests
    PRIVATE
        glm::glm
        Jolt::Jolt
        yaml-cpp::yaml-cpp
)

add_test(NAME manual_engine_terrain_physics_collider_adapter_tests COMMAND manual_engine_terrain_physics_collider_adapter_tests)

add_executable(manual_engine_terrain_material_metadata_tests
    tests/terrain/TerrainMaterialMetadataTestRunner.cpp
    src/Assets/Assimp/Importer.cpp
    src/Engine/AssetRegistry.cpp
    src/Engine/Biome.cpp
    src/Engine/TerrainDataset.cpp
    src/Engine/TerrainMaterialMetadata.cpp
)

target_include_directories(manual_engine_terrain_material_metadata_tests
    PRIVATE
        src
)

target_link_libraries(manual_engine_terrain_material_metadata_tests
    PRIVATE
        assimp::assimp
        glm::glm
        yaml-cpp::yaml-cpp
)

add_test(NAME manual_engine_terrain_material_metadata_tests COMMAND manual_engine_terrain_material_metadata_tests)

add_executable(manual_engine_terrain_layered_rendering_tests
    tests/terrain/TerrainLayeredRenderingTestRunner.cpp
    tests/authored_scene/RendererOwnershipStubs.cpp
    src/Assets/Assimp/Importer.cpp
    src/Engine/AssetCache.cpp
    src/Engine/AssetRegistry.cpp
    src/Engine/Biome.cpp
    src/Engine/TerrainDataset.cpp
    src/Engine/TerrainMaterialMetadata.cpp
    src/Engine/TerrainMaterialRenderAdapter.cpp
    src/Engine/TerrainSampleMaterials.cpp
)

target_include_directories(manual_engine_terrain_layered_rendering_tests
    PRIVATE
        src
)

target_compile_definitions(manual_engine_terrain_layered_rendering_tests
    PRIVATE
        MANUAL_ENGINE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(manual_engine_terrain_layered_rendering_tests
    PRIVATE
        assimp::assimp
        glm::glm
        yaml-cpp::yaml-cpp
)

add_test(NAME manual_engine_terrain_layered_rendering_tests COMMAND manual_engine_terrain_layered_rendering_tests)

add_executable(manual_engine_terrain_serialization_prep_tests
    tests/terrain/TerrainSerializationPrepTestRunner.cpp
    src/Engine/TerrainDerivedCache.cpp
    src/Engine/TerrainSerializationPrep.cpp
)

target_include_directories(manual_engine_terrain_serialization_prep_tests
    PRIVATE
        src
)

target_link_libraries(manual_engine_terrain_serialization_prep_tests
    PRIVATE
        glm::glm
        yaml-cpp::yaml-cpp
)

add_test(NAME manual_engine_terrain_serialization_prep_tests COMMAND manual_engine_terrain_serialization_prep_tests)

add_executable(manual_engine_asset_registry_tests
    tests/assets/AssetRegistryTestRunner.cpp
    src/Assets/Assimp/Importer.cpp
    src/Engine/AssetRegistry.cpp
)

target_include_directories(manual_engine_asset_registry_tests
    PRIVATE
        src
)

target_link_libraries(manual_engine_asset_registry_tests
    PRIVATE
        assimp::assimp
        glm::glm
)

add_test(NAME manual_engine_asset_registry_tests COMMAND manual_engine_asset_registry_tests)

add_executable(manual_engine_scene_tests
    tests/scene/SceneTestRunner.cpp
    src/Engine/Scene/Scene.cpp
)

target_include_directories(manual_engine_scene_tests
    PRIVATE
        src
)

target_link_libraries(manual_engine_scene_tests
    PRIVATE
        glm::glm
)

add_test(NAME manual_engine_scene_tests COMMAND manual_engine_scene_tests)

add_executable(manual_engine_actor_authoring_tests
    tests/scene/ActorAuthoringTestRunner.cpp
    src/Engine/ActorAuthoring.cpp
    src/Engine/Reflection.cpp
    src/Engine/Scene/Scene.cpp
)

target_include_directories(manual_engine_actor_authoring_tests
    PRIVATE
        src
)

target_link_libraries(manual_engine_actor_authoring_tests
    PRIVATE
        glm::glm
)

add_test(NAME manual_engine_actor_authoring_tests COMMAND manual_engine_actor_authoring_tests)

add_executable(manual_engine_actor_component_authoring_tests
    tests/scene/ActorComponentAuthoringTestRunner.cpp
    src/Engine/ActorAuthoring.cpp
    src/Engine/ActorComponentAuthoring.cpp
    src/Engine/Reflection.cpp
    src/Engine/Scene/Scene.cpp
)

target_include_directories(manual_engine_actor_component_authoring_tests
    PRIVATE
        src
)

target_link_libraries(manual_engine_actor_component_authoring_tests
    PRIVATE
        glm::glm
)

add_test(NAME manual_engine_actor_component_authoring_tests COMMAND manual_engine_actor_component_authoring_tests)

add_executable(manual_engine_scene_render_bridge_tests
    tests/scene/SceneRenderBridgeTestRunner.cpp
    src/Engine/Scene/Scene.cpp
    src/Engine/Scene/SceneRenderBridge.cpp
)

target_include_directories(manual_engine_scene_render_bridge_tests
    PRIVATE
        src
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
)

target_link_libraries(manual_engine_scene_render_bridge_tests
    PRIVATE
        glm::glm
)

add_test(NAME manual_engine_scene_render_bridge_tests COMMAND manual_engine_scene_render_bridge_tests)

add_executable(manual_engine_scene_physics_tests
    tests/scene/ScenePhysicsTestRunner.cpp
    src/Engine/Physics/ScenePhysics.cpp
    src/Engine/Scene/Scene.cpp
)

target_include_directories(manual_engine_scene_physics_tests
    PRIVATE
        src
)

target_link_libraries(manual_engine_scene_physics_tests
    PRIVATE
        glm::glm
        Jolt::Jolt
)

add_test(NAME manual_engine_scene_physics_tests COMMAND manual_engine_scene_physics_tests)

add_executable(manual_engine_scene_reflection_tests
    tests/scene/SceneReflectionTestRunner.cpp
    tests/navigation/RendererStubs.cpp
    src/Assets/Assimp/Importer.cpp
    src/Engine/AssetRegistry.cpp
    src/Engine/Biome.cpp
    src/Engine/ChunkTypes.cpp
    src/Engine/Navigation.cpp
    src/Engine/NavigationConnectivity.cpp
    src/Engine/NavigationRuntime.cpp
    src/Engine/Physics/ScenePhysics.cpp
    src/Engine/Reflection.cpp
    src/Engine/Scene/Scene.cpp
    src/Engine/Scene/SceneRenderBridge.cpp
    src/Engine/SceneCharacterMovement.cpp
   src/Engine/SceneReflection.cpp
    src/Engine/TerrainDataset.cpp
)

target_include_directories(manual_engine_scene_reflection_tests
    PRIVATE
        src
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
)

target_link_libraries(manual_engine_scene_reflection_tests
    PRIVATE
        glm::glm
        assimp::assimp
        Jolt::Jolt
        RecastNavigation::Recast
        RecastNavigation::Detour
        yaml-cpp::yaml-cpp
)

add_test(NAME manual_engine_scene_reflection_tests COMMAND manual_engine_scene_reflection_tests)

add_executable(manual_engine_scene_behavior_hooks_tests
    tests/scene/SceneBehaviorHooksTestRunner.cpp
    tests/navigation/RendererStubs.cpp
    src/Assets/Assimp/Importer.cpp
    src/Engine/AssetRegistry.cpp
    src/Engine/Biome.cpp
    src/Engine/ChunkTypes.cpp
    src/Engine/Navigation.cpp
    src/Engine/NavigationConnectivity.cpp
    src/Engine/NavigationRuntime.cpp
    src/Engine/Physics/ScenePhysics.cpp
    src/Engine/Reflection.cpp
    src/Engine/Scene/Scene.cpp
    src/Engine/Scene/SceneRenderBridge.cpp
    src/Engine/SceneBehaviorHooks.cpp
    src/Engine/SceneCharacterMovement.cpp
   src/Engine/SceneReflection.cpp
    src/Engine/TerrainDataset.cpp
)

target_include_directories(manual_engine_scene_behavior_hooks_tests
    PRIVATE
        src
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
)

target_link_libraries(manual_engine_scene_behavior_hooks_tests
    PRIVATE
        glm::glm
        assimp::assimp
        Jolt::Jolt
        RecastNavigation::Recast
        RecastNavigation::Detour
        yaml-cpp::yaml-cpp
)

add_test(NAME manual_engine_scene_behavior_hooks_tests COMMAND manual_engine_scene_behavior_hooks_tests)

add_executable(manual_engine_scene_lua_scripting_tests
    tests/scene/SceneLuaScriptingTestRunner.cpp
    tests/navigation/RendererStubs.cpp
    src/Assets/Assimp/Importer.cpp
    src/Engine/AssetRegistry.cpp
    src/Engine/Biome.cpp
    src/Engine/ChunkTypes.cpp
    src/Engine/Navigation.cpp
    src/Engine/NavigationConnectivity.cpp
    src/Engine/NavigationRuntime.cpp
    src/Engine/Physics/ScenePhysics.cpp
    src/Engine/Reflection.cpp
    src/Engine/Scene/Scene.cpp
    src/Engine/Scene/SceneRenderBridge.cpp
    src/Engine/SceneBehaviorHooks.cpp
    src/Engine/SceneCharacterMovement.cpp
    src/Engine/SceneLuaScripting.cpp
   src/Engine/SceneReflection.cpp
    src/Engine/TerrainDataset.cpp
)

target_include_directories(manual_engine_scene_lua_scripting_tests
    PRIVATE
        src
        ${LUA_INCLUDE_DIR}
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
)

target_link_libraries(manual_engine_scene_lua_scripting_tests
    PRIVATE
        glm::glm
        assimp::assimp
        Jolt::Jolt
        ${LUA_LIBRARIES}
        RecastNavigation::Recast
        RecastNavigation::Detour
        yaml-cpp::yaml-cpp
)

add_test(NAME manual_engine_scene_lua_scripting_tests COMMAND manual_engine_scene_lua_scripting_tests)

add_executable(manual_engine_scene_serialization_tests
    tests/scene/SceneSerializationTestRunner.cpp
    src/Engine/ActorAuthoring.cpp
    src/Engine/ActorComponentAuthoring.cpp
    src/Engine/Reflection.cpp
    src/Engine/Scene/Scene.cpp
    src/Engine/SceneSerialization.cpp
    src/Engine/TerrainSerializationPrep.cpp
)

target_include_directories(manual_engine_scene_serialization_tests
    PRIVATE
        src
)

target_link_libraries(manual_engine_scene_serialization_tests
    PRIVATE
        glm::glm
        yaml-cpp::yaml-cpp
)

add_test(NAME manual_engine_scene_serialization_tests COMMAND manual_engine_scene_serialization_tests)

add_executable(manual_engine_scene_terrain_alignment_tests
    tests/scene/SceneTerrainAlignmentTestRunner.cpp
    tests/navigation/RendererStubs.cpp
    src/Engine/Biome.cpp
    src/Engine/ChunkTypes.cpp
    src/Engine/Navigation.cpp
    src/Engine/NavigationConnectivity.cpp
    src/Engine/NavigationRuntime.cpp
    src/Engine/Physics/ScenePhysics.cpp
    src/Engine/Scene/Scene.cpp
   src/Engine/TerrainDataset.cpp
    src/Engine/TerrainNavigationAdapter.cpp
    src/Engine/TerrainPhysicsColliderAdapter.cpp
    src/Engine/TerrainSerializationPrep.cpp
)

target_include_directories(manual_engine_scene_terrain_alignment_tests
    PRIVATE
        src
)

target_link_libraries(manual_engine_scene_terrain_alignment_tests
    PRIVATE
        glm::glm
        Jolt::Jolt
        RecastNavigation::Recast
        RecastNavigation::Detour
        yaml-cpp::yaml-cpp
)

add_test(NAME manual_engine_scene_terrain_alignment_tests COMMAND manual_engine_scene_terrain_alignment_tests)

add_executable(manual_engine_scene_character_movement_tests
    tests/scene/SceneCharacterMovementTestRunner.cpp
    tests/navigation/RendererStubs.cpp
    src/Engine/Biome.cpp
    src/Engine/ChunkTypes.cpp
    src/Engine/Navigation.cpp
    src/Engine/NavigationConnectivity.cpp
    src/Engine/NavigationRuntime.cpp
    src/Engine/Physics/ScenePhysics.cpp
    src/Engine/Scene/Scene.cpp
    src/Engine/SceneCharacterMovement.cpp
   src/Engine/TerrainDataset.cpp
    src/Engine/TerrainNavigationAdapter.cpp
    src/Engine/TerrainPhysicsColliderAdapter.cpp
)

target_include_directories(manual_engine_scene_character_movement_tests
    PRIVATE
        src
)

target_link_libraries(manual_engine_scene_character_movement_tests
    PRIVATE
        glm::glm
        Jolt::Jolt
        RecastNavigation::Recast
        RecastNavigation::Detour
        yaml-cpp::yaml-cpp
)

add_test(NAME manual_engine_scene_character_movement_tests COMMAND manual_engine_scene_character_movement_tests)


add_executable(manual_engine_authored_scene_adapter_tests
    tests/scene/AuthoredSceneAdapterTestRunner.cpp
    tests/authored_scene/RendererOwnershipStubs.cpp
    src/Assets/Assimp/Importer.cpp
    src/Engine/AssetCache.cpp
    src/Engine/ImportedSceneResources.cpp
    src/Engine/NavigationSceneGeometry.cpp
    src/Engine/Scene/AuthoredSceneAdapter.cpp
    src/Engine/Scene/Scene.cpp
    src/Engine/Scene/SceneRenderBridge.cpp
)

target_include_directories(manual_engine_authored_scene_adapter_tests
    PRIVATE
        src
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
)

target_link_libraries(manual_engine_authored_scene_adapter_tests
    PRIVATE
        assimp::assimp
        glm::glm
)

add_test(NAME manual_engine_authored_scene_adapter_tests COMMAND manual_engine_authored_scene_adapter_tests)

add_executable(manual_engine_animated_scene_adapter_tests
    tests/scene/AnimatedSceneAdapterTestRunner.cpp
    tests/authored_scene/RendererOwnershipStubs.cpp
    src/Assets/Assimp/Importer.cpp
   src/Engine/AnimatedModelPose.cpp
    src/Engine/AssetCache.cpp
    src/Engine/ImportedSceneResources.cpp
    src/Engine/Scene/AnimatedSceneAdapter.cpp
    src/Engine/Scene/Scene.cpp
    src/Engine/Scene/SceneRenderBridge.cpp
)

target_include_directories(manual_engine_animated_scene_adapter_tests
    PRIVATE
        src
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
)

target_link_libraries(manual_engine_animated_scene_adapter_tests
    PRIVATE
        assimp::assimp
        glm::glm
        yaml-cpp::yaml-cpp
)

add_test(NAME manual_engine_animated_scene_adapter_tests COMMAND manual_engine_animated_scene_adapter_tests)


add_executable(manual_engine_texture_tests
    tests/renderer/TextureTestRunner.cpp
    src/Renderer/Texture.cpp
    src/Renderer/core.cpp
)

target_include_directories(manual_engine_texture_tests
    PRIVATE
        src
        ${Stb_INCLUDE_DIR}
)

target_link_libraries(manual_engine_texture_tests
    PRIVATE
        SDL3::SDL3
        bgfx::bgfx
)

add_test(NAME manual_engine_texture_tests COMMAND manual_engine_texture_tests)


set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/src")
set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/dx11")
find_program(BGFX_SHADERC shaderc
    HINTS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/bgfx"
    REQUIRED
)

add_custom_command(
    OUTPUT
        "${SHADER_OUTPUT_DIR}/vs_mesh.bin"
        "${SHADER_OUTPUT_DIR}/vs_terrain.bin"
        "${SHADER_OUTPUT_DIR}/vs_skinned_mesh.bin"
        "${SHADER_OUTPUT_DIR}/fs_mesh.bin"
        "${SHADER_OUTPUT_DIR}/fs_terrain.bin"
        "${SHADER_OUTPUT_DIR}/vs_imgui.bin"
        "${SHADER_OUTPUT_DIR}/fs_imgui.bin"
        "${SHADER_OUTPUT_DIR}/vs_debug_line.bin"
        "${SHADER_OUTPUT_DIR}/fs_debug_line.bin"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${SHADER_OUTPUT_DIR}"
    COMMAND "${BGFX_SHADERC}"
        -f "${SHADER_SOURCE_DIR}/vs_mesh.vs"
        -o "${SHADER_OUTPUT_DIR}/vs_mesh.bin"
        --type vertex
        --platform windows
        -p s_5_0
        --varyingdef "${SHADER_SOURCE_DIR}/varying.def.sc"
        -i "${SHADER_SOURCE_DIR}"
        -i "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/bgfx"
    COMMAND "${BGFX_SHADERC}"
        -f "${SHADER_SOURCE_DIR}/fs_mesh.fs"
        -o "${SHADER_OUTPUT_DIR}/fs_mesh.bin"
        --type fragment
        --platform windows
        -p s_5_0
        --varyingdef "${SHADER_SOURCE_DIR}/varying.def.sc"
        -i "${SHADER_SOURCE_DIR}"
        -i "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/bgfx"
    COMMAND "${BGFX_SHADERC}"
        -f "${SHADER_SOURCE_DIR}/vs_terrain.vs"
        -o "${SHADER_OUTPUT_DIR}/vs_terrain.bin"
        --type vertex
        --platform windows
        -p s_5_0
        --varyingdef "${SHADER_SOURCE_DIR}/varying.def.sc"
        -i "${SHADER_SOURCE_DIR}"
        -i "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/bgfx"
    COMMAND "${BGFX_SHADERC}"
        -f "${SHADER_SOURCE_DIR}/fs_terrain.fs"
        -o "${SHADER_OUTPUT_DIR}/fs_terrain.bin"
        --type fragment
        --platform windows
        -p s_5_0
        --varyingdef "${SHADER_SOURCE_DIR}/varying.def.sc"
        -i "${SHADER_SOURCE_DIR}"
        -i "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/bgfx"
    COMMAND "${BGFX_SHADERC}"
        -f "${SHADER_SOURCE_DIR}/vs_skinned_mesh.vs"
        -o "${SHADER_OUTPUT_DIR}/vs_skinned_mesh.bin"
        --type vertex
        --platform windows
        -p s_5_0
        --varyingdef "${SHADER_SOURCE_DIR}/varying.def.sc"
        -i "${SHADER_SOURCE_DIR}"
        -i "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/bgfx"
    COMMAND "${BGFX_SHADERC}"
        -f "${SHADER_SOURCE_DIR}/vs_imgui.vs"
        -o "${SHADER_OUTPUT_DIR}/vs_imgui.bin"
        --type vertex
        --platform windows
        -p s_5_0
        --varyingdef "${SHADER_SOURCE_DIR}/varying.def.sc"
        -i "${SHADER_SOURCE_DIR}"
        -i "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/bgfx"
    COMMAND "${BGFX_SHADERC}"
        -f "${SHADER_SOURCE_DIR}/fs_imgui.fs"
        -o "${SHADER_OUTPUT_DIR}/fs_imgui.bin"
        --type fragment
        --platform windows
        -p s_5_0
        --varyingdef "${SHADER_SOURCE_DIR}/varying.def.sc"
        -i "${SHADER_SOURCE_DIR}"
        -i "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/bgfx"
    COMMAND "${BGFX_SHADERC}"
        -f "${SHADER_SOURCE_DIR}/vs_debug_line.vs"
        -o "${SHADER_OUTPUT_DIR}/vs_debug_line.bin"
        --type vertex
        --platform windows
        -p s_5_0
        --varyingdef "${SHADER_SOURCE_DIR}/varying.def.sc"
        -i "${SHADER_SOURCE_DIR}"
        -i "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/bgfx"
    COMMAND "${BGFX_SHADERC}"
        -f "${SHADER_SOURCE_DIR}/fs_debug_line.fs"
        -o "${SHADER_OUTPUT_DIR}/fs_debug_line.bin"
        --type fragment
        --platform windows
        -p s_5_0
        --varyingdef "${SHADER_SOURCE_DIR}/varying.def.sc"
        -i "${SHADER_SOURCE_DIR}"
        -i "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/bgfx"
    DEPENDS
        "${SHADER_SOURCE_DIR}/vs_mesh.vs"
        "${SHADER_SOURCE_DIR}/vs_terrain.vs"
        "${SHADER_SOURCE_DIR}/vs_skinned_mesh.vs"
        "${SHADER_SOURCE_DIR}/fs_mesh.fs"
        "${SHADER_SOURCE_DIR}/fs_terrain.fs"
        "${SHADER_SOURCE_DIR}/vs_imgui.vs"
        "${SHADER_SOURCE_DIR}/fs_imgui.fs"
        "${SHADER_SOURCE_DIR}/vs_debug_line.vs"
        "${SHADER_SOURCE_DIR}/fs_debug_line.fs"
        "${SHADER_SOURCE_DIR}/varying.def.sc"
    VERBATIM
)

add_custom_target(manual_engine_shaders
    DEPENDS
        "${SHADER_OUTPUT_DIR}/vs_mesh.bin"
        "${SHADER_OUTPUT_DIR}/vs_terrain.bin"
        "${SHADER_OUTPUT_DIR}/vs_skinned_mesh.bin"
        "${SHADER_OUTPUT_DIR}/fs_mesh.bin"
        "${SHADER_OUTPUT_DIR}/fs_terrain.bin"
        "${SHADER_OUTPUT_DIR}/vs_imgui.bin"
        "${SHADER_OUTPUT_DIR}/fs_imgui.bin"
        "${SHADER_OUTPUT_DIR}/vs_debug_line.bin"
        "${SHADER_OUTPUT_DIR}/fs_debug_line.bin"
)

add_dependencies(manual_engine manual_engine_shaders)
