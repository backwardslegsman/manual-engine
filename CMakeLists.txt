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
find_package(recastnavigation CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)

enable_testing()

add_executable(manual_engine
    src/App/main.cpp
    src/Assets/Assimp/Importer.cpp
    src/Engine/ActorCommand.cpp
    src/Engine/ActorController.cpp
    src/Engine/ActorSelection.cpp
    src/Engine/AssetCache.cpp
    src/Engine/Biome.cpp
    src/Engine/BlockingCollision.cpp
    src/Engine/ChunkStreamer.cpp
    src/Engine/ChunkTypes.cpp
    src/Engine/EventQueue.cpp
    src/Engine/FixedStepLoop.cpp
    src/Engine/InputMapping.cpp
    src/Engine/InteractionHandlerSystem.cpp
    src/Engine/InteractionSystem.cpp
    src/Engine/NavigationConnectivity.cpp
    src/Engine/NavigationCache.cpp
    src/Engine/NavigationProfile.cpp
    src/Engine/ObjectArchetype.cpp
    src/Engine/ObjectId.cpp
    src/Engine/Navigation.cpp
    src/Engine/OrbitCamera.cpp
    src/Engine/PersistentObjectEditor.cpp
    src/Engine/Picking.cpp
    src/Engine/ProceduralChunkContent.cpp
    src/Engine/SpatialRegistry.cpp
    src/Engine/Terrain.cpp
    src/Engine/World.cpp
    src/Engine/WorldNavigationGraph.cpp
    src/Engine/WorldObjectOverrides.cpp
    src/Engine/WorldState.cpp
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
        RecastNavigation::Recast
        RecastNavigation::Detour
        RecastNavigation::DetourTileCache
        RecastNavigation::DetourCrowd
        RecastNavigation::DebugUtils
        yaml-cpp::yaml-cpp
)

add_executable(manual_engine_navigation_tests
    tests/navigation/NavigationTestRunner.cpp
    tests/navigation/RendererStubs.cpp
    src/Engine/ActorController.cpp
    src/Engine/Biome.cpp
    src/Engine/BlockingCollision.cpp
    src/Engine/ChunkTypes.cpp
    src/Engine/EventQueue.cpp
    src/Engine/NavigationConnectivity.cpp
    src/Engine/NavigationProfile.cpp
    src/Engine/ObjectId.cpp
    src/Engine/Navigation.cpp
    src/Engine/SpatialRegistry.cpp
    src/Engine/Terrain.cpp
    src/Engine/World.cpp
    src/Engine/WorldNavigationGraph.cpp
)

target_include_directories(manual_engine_navigation_tests
    PRIVATE
        src
)

target_link_libraries(manual_engine_navigation_tests
    PRIVATE
        glm::glm
        RecastNavigation::Recast
        RecastNavigation::Detour
        yaml-cpp::yaml-cpp
)

add_test(NAME manual_engine_navigation_tests COMMAND manual_engine_navigation_tests)

set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/src")
set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/dx11")
find_program(BGFX_SHADERC shaderc
    HINTS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/bgfx"
    REQUIRED
)

add_custom_command(
    OUTPUT
        "${SHADER_OUTPUT_DIR}/vs_mesh.bin"
        "${SHADER_OUTPUT_DIR}/fs_mesh.bin"
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
        "${SHADER_SOURCE_DIR}/fs_mesh.fs"
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
        "${SHADER_OUTPUT_DIR}/fs_mesh.bin"
        "${SHADER_OUTPUT_DIR}/vs_imgui.bin"
        "${SHADER_OUTPUT_DIR}/fs_imgui.bin"
        "${SHADER_OUTPUT_DIR}/vs_debug_line.bin"
        "${SHADER_OUTPUT_DIR}/fs_debug_line.bin"
)

add_dependencies(manual_engine manual_engine_shaders)
