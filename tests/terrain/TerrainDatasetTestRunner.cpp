#include <algorithm>
#include <cmath>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Engine/TerrainDataset.hpp"
#include "Engine/TerrainDatasetCompatibility.hpp"

namespace {
    struct TestFailure {
        std::string testName;
        std::string message;
    };

    struct TestContext {
        std::string name;
        std::vector<TestFailure>& failures;

        void expect(bool condition, std::string message)
        {
            if (!condition) {
                failures.push_back({name, std::move(message)});
            }
        }
    };

    bool near(float lhs, float rhs, float epsilon = 0.001f)
    {
        return std::fabs(lhs - rhs) <= epsilon;
    }

    Engine::TerrainSourceDescriptor importedSourceDescriptor(uint64_t id = 101)
    {
        Engine::TerrainSourceDescriptor descriptor;
        descriptor.sourceId = {id};
        descriptor.type = Engine::TerrainDatasetSourceType::HeightmapImported;
        descriptor.defaultChunkSize = 2.0f;
        descriptor.defaultResolution = 3;
        descriptor.debugName = "imported.test";
        descriptor.settings = {"heightmap_terrain", "1", "test"};
        return descriptor;
    }

    Engine::TerrainSourceDescriptor proceduralSourceDescriptor(uint64_t id = 202)
    {
        Engine::TerrainSourceDescriptor descriptor;
        descriptor.sourceId = {id};
        descriptor.type = Engine::TerrainDatasetSourceType::Procedural;
        descriptor.defaultChunkSize = 4.0f;
        descriptor.defaultResolution = 5;
        descriptor.debugName = "procedural.test";
        descriptor.settings = {"procedural_terrain", "1", "test"};
        descriptor.procedural.chunkSize = 4.0f;
        descriptor.procedural.resolution = 5;
        descriptor.procedural.heightScale = 1.0f;
        return descriptor;
    }

    Engine::TerrainImportedChunk importedChunk(
        Engine::AssetId sourceId = {101},
        Engine::TerrainSourceChunkCoord coord = {0, 0},
        glm::vec3 origin = {0.0f, 0.0f, 0.0f})
    {
        Engine::TerrainImportedChunk chunk;
        chunk.id = {sourceId, coord};
        chunk.coord = coord;
        chunk.origin = origin;
        chunk.size = 2.0f;
        chunk.resolution = 3;
        chunk.heights = {
            0.0f, 1.0f, 2.0f,
            1.0f, 2.0f, 3.0f,
            2.0f, 3.0f, 4.0f,
        };
        return chunk;
    }

    void registerSourceCreatesStableRuntimeHandle(TestContext& ctx)
    {
        Engine::TerrainDataset dataset;
        const Engine::TerrainSourceHandle source = dataset.registerSource(importedSourceDescriptor());
        const std::optional<Engine::TerrainSourceDescriptor> metadata = dataset.sourceMetadata(source);

        ctx.expect(Engine::isValid(source), "registered source handle was invalid");
        ctx.expect(dataset.contains(source), "dataset did not contain registered source");
        ctx.expect(metadata.has_value(), "registered source metadata was missing");
        if (metadata) {
            ctx.expect(metadata->sourceId == Engine::AssetId{101}, "source stable ID was not preserved");
            ctx.expect(metadata->type == Engine::TerrainDatasetSourceType::HeightmapImported, "source type was wrong");
        }
        ctx.expect(dataset.diagnostics().sourceCount == 1, "source diagnostics count was wrong");
    }

    void sourceSlotReuseInvalidatesStaleHandle(TestContext& ctx)
    {
        Engine::TerrainDataset dataset;
        const Engine::TerrainSourceHandle first = dataset.registerSource(importedSourceDescriptor());
        ctx.expect(dataset.unregisterSource(first), "unregister source failed");
        const Engine::TerrainSourceHandle second = dataset.registerSource(importedSourceDescriptor(102));

        ctx.expect(!dataset.contains(first), "stale source handle still validated");
        ctx.expect(dataset.contains(second), "reused source handle did not validate");
        ctx.expect(first.index == second.index, "source slot was not reused");
        ctx.expect(first.generation != second.generation, "source generation did not increment on reuse");
    }

    void loadImportedChunkStoresCpuData(TestContext& ctx)
    {
        Engine::TerrainDataset dataset;
        const Engine::TerrainSourceHandle source = dataset.registerSource(importedSourceDescriptor());
        const Engine::TerrainChunkHandle chunk = dataset.loadImportedChunk(source, importedChunk());
        const std::optional<Engine::TerrainChunkData> data = dataset.chunk(chunk);

        ctx.expect(Engine::isValid(chunk), "imported chunk handle was invalid");
        ctx.expect(data.has_value(), "imported chunk data was missing");
        if (data) {
            ctx.expect(data->source == source, "imported chunk source handle was wrong");
            ctx.expect(data->sourceType == Engine::TerrainDatasetSourceType::HeightmapImported, "imported chunk source type was wrong");
            ctx.expect(data->heights.size() == 9, "imported chunk heights were not stored");
        }
        ctx.expect(dataset.diagnostics().loadedChunkCount == 1, "loaded chunk diagnostics count was wrong");
        ctx.expect(dataset.diagnostics().importedChunkCount == 1, "imported chunk diagnostics count was wrong");
    }

    void unloadChunkInvalidatesHandle(TestContext& ctx)
    {
        Engine::TerrainDataset dataset;
        const Engine::TerrainSourceHandle source = dataset.registerSource(importedSourceDescriptor());
        const Engine::TerrainChunkHandle first = dataset.loadImportedChunk(source, importedChunk());
        ctx.expect(dataset.unloadChunk(first), "unload chunk failed");
        const Engine::TerrainChunkHandle second = dataset.loadImportedChunk(source, importedChunk());

        ctx.expect(!dataset.contains(first), "stale chunk handle still validated");
        ctx.expect(dataset.contains(second), "reloaded chunk handle did not validate");
        ctx.expect(first.index == second.index, "chunk slot was not reused");
        ctx.expect(first.generation != second.generation, "chunk generation did not increment on reuse");
    }

    void unregisterSourceUnloadsChunks(TestContext& ctx)
    {
        Engine::TerrainDataset dataset;
        const Engine::TerrainSourceHandle source = dataset.registerSource(importedSourceDescriptor());
        const Engine::TerrainChunkHandle chunk = dataset.loadImportedChunk(source, importedChunk());

        ctx.expect(dataset.unregisterSource(source), "unregister source failed");
        ctx.expect(!dataset.contains(source), "unregistered source still validated");
        ctx.expect(!dataset.contains(chunk), "source chunk was not unloaded");
        ctx.expect(dataset.chunks().empty(), "dataset still listed unloaded chunk");
    }

    void sampleHeightUsesImportedChunkData(TestContext& ctx)
    {
        Engine::TerrainDataset dataset;
        const Engine::TerrainSourceHandle source = dataset.registerSource(importedSourceDescriptor());
        [[maybe_unused]] const Engine::TerrainChunkHandle chunk = dataset.loadImportedChunk(source, importedChunk());

        const std::optional<float> center = dataset.sampleHeight(1.0f, 1.0f);
        const std::optional<float> quarter = dataset.sampleHeight(0.5f, 0.5f);
        ctx.expect(center.has_value() && near(*center, 2.0f), "center imported height sample was wrong");
        ctx.expect(quarter.has_value() && near(*quarter, 1.0f), "interpolated imported height sample was wrong");
        ctx.expect(!dataset.sampleHeight(4.0f, 4.0f).has_value(), "sample outside loaded chunk unexpectedly succeeded");
    }

    void raycastHitsImportedChunk(TestContext& ctx)
    {
        Engine::TerrainDataset dataset;
        const Engine::TerrainSourceHandle source = dataset.registerSource(importedSourceDescriptor());
        const Engine::TerrainChunkHandle chunk = dataset.loadImportedChunk(source, importedChunk());

        const std::optional<Engine::TerrainDatasetRaycastHit> hit = dataset.raycast(
            {{1.0f, 8.0f, 1.0f}, {0.0f, -1.0f, 0.0f}},
            20.0f,
            0.5f);
        ctx.expect(hit.has_value(), "raycast did not hit imported chunk");
        if (hit) {
            ctx.expect(hit->chunk == chunk, "raycast hit wrong chunk");
            ctx.expect(near(hit->position.y, 2.0f), "raycast hit height was wrong");
        }
    }

    void chunkWorldBoundsAndDiagnostics(TestContext& ctx)
    {
        Engine::TerrainDataset dataset;
        const Engine::TerrainSourceHandle source = dataset.registerSource(importedSourceDescriptor());
        const Engine::TerrainChunkHandle chunk = dataset.loadImportedChunk(source, importedChunk());

        const std::optional<Engine::TerrainDatasetBounds> bounds = dataset.chunkWorldBounds(chunk);
        const std::optional<Engine::TerrainChunkDiagnostics> diagnostics = dataset.chunkDiagnostics(chunk);
        ctx.expect(bounds.has_value(), "chunk bounds were missing");
        if (bounds) {
            ctx.expect(near(bounds->min.x, 0.0f) && near(bounds->max.x, 2.0f), "chunk bounds x range was wrong");
            ctx.expect(near(bounds->min.y, 0.0f) && near(bounds->max.y, 4.0f), "chunk bounds y range was wrong");
        }
        ctx.expect(diagnostics.has_value() && diagnostics->valid, "chunk diagnostics were missing");
        if (diagnostics) {
            ctx.expect(near(diagnostics->minHeight, 0.0f), "diagnostic min height was wrong");
            ctx.expect(near(diagnostics->maxHeight, 4.0f), "diagnostic max height was wrong");
            ctx.expect(near(diagnostics->averageHeight, 2.0f), "diagnostic average height was wrong");
            ctx.expect(diagnostics->maxSlopeDegrees > 0.0f, "diagnostic slope was not computed");
        }
    }

    void proceduralSourceLoadsChunkThroughSameQueryPath(TestContext& ctx)
    {
        Engine::TerrainDataset dataset;
        const Engine::TerrainSourceHandle source = dataset.registerSource(proceduralSourceDescriptor());
        const Engine::TerrainChunkHandle chunk = dataset.loadProceduralChunk(source, {0, 0});
        const std::optional<Engine::TerrainChunkData> data = dataset.chunk(chunk);
        const std::optional<float> height = dataset.sampleHeight(1.0f, 1.0f);

        ctx.expect(Engine::isValid(chunk), "procedural chunk handle was invalid");
        ctx.expect(data.has_value() && data->sourceType == Engine::TerrainDatasetSourceType::Procedural, "procedural chunk data was wrong");
        ctx.expect(height.has_value(), "procedural chunk could not be sampled through dataset");
        ctx.expect(dataset.diagnostics().proceduralChunkCount == 1, "procedural diagnostic count was wrong");
    }

    void importedAndProceduralChunksShareRuntimeQueries(TestContext& ctx)
    {
        Engine::TerrainDataset dataset;
        const Engine::TerrainSourceHandle imported = dataset.registerSource(importedSourceDescriptor());
        const Engine::TerrainSourceHandle procedural = dataset.registerSource(proceduralSourceDescriptor());
        [[maybe_unused]] const Engine::TerrainChunkHandle importedChunkHandle =
            dataset.loadImportedChunk(imported, importedChunk({101}, {0, 0}, {0.0f, 0.0f, 0.0f}));
        [[maybe_unused]] const Engine::TerrainChunkHandle proceduralChunkHandle =
            dataset.loadProceduralChunk(procedural, {1, 0});

        ctx.expect(dataset.sampleHeight(1.0f, 1.0f).has_value(), "imported chunk was not queryable");
        ctx.expect(dataset.sampleHeight(5.0f, 1.0f).has_value(), "procedural chunk was not queryable");
        ctx.expect(dataset.chunks().size() == 2, "dataset did not list both chunk types");
    }

    void invalidInputsAreNoOps(TestContext& ctx)
    {
        Engine::TerrainDataset dataset;
        const Engine::TerrainSourceHandle imported = dataset.registerSource(importedSourceDescriptor());
        Engine::TerrainImportedChunk malformed = importedChunk();
        malformed.heights.pop_back();

        ctx.expect(!dataset.unregisterSource({}), "invalid source unregister unexpectedly succeeded");
        ctx.expect(!Engine::isValid(dataset.loadImportedChunk({}, importedChunk())), "default source accepted imported chunk");
        ctx.expect(!Engine::isValid(dataset.loadImportedChunk(imported, malformed)), "malformed imported chunk was accepted");
        ctx.expect(!Engine::isValid(dataset.loadProceduralChunk(imported, {0, 0})), "imported source accepted procedural chunk");
        ctx.expect(!dataset.unloadChunk({}), "invalid chunk unload unexpectedly succeeded");
        ctx.expect(dataset.diagnostics().invalidRequestCount >= 5, "invalid request diagnostics were not counted");
    }

    void datasetChunkConvertsToGeneratedTerrainTileData(TestContext& ctx)
    {
        Engine::TerrainDataset dataset;
        const Engine::TerrainSourceHandle source = dataset.registerSource(importedSourceDescriptor());
        const Engine::TerrainChunkHandle chunk = dataset.loadImportedChunk(source, importedChunk({101}, {3, 4}, {6.0f, 0.0f, 8.0f}));
        const std::optional<Engine::GeneratedTerrainTileData> generated = Engine::toGeneratedTerrainTileData(dataset, chunk);

        ctx.expect(generated.has_value(), "dataset chunk did not convert to generated terrain data");
        if (generated) {
            ctx.expect(generated->coord == Engine::ChunkCoord{3, 4}, "generated terrain coord was wrong");
            ctx.expect(near(generated->origin.x, 6.0f) && near(generated->origin.z, 8.0f), "generated terrain origin was wrong");
            ctx.expect(generated->heights.size() == 9, "generated terrain heights were wrong");
        }
    }

    void compatibilityTerrainSystemMatchesDatasetSampling(TestContext& ctx)
    {
        Engine::TerrainDataset dataset;
        const Engine::TerrainSourceHandle source = dataset.registerSource(importedSourceDescriptor());
        const Engine::TerrainChunkHandle chunk = dataset.loadImportedChunk(source, importedChunk({101}, {0, 0}, {0.0f, 0.0f, 0.0f}));
        const std::optional<Engine::GeneratedTerrainTileData> generated = Engine::toGeneratedTerrainTileData(dataset, chunk);
        ctx.expect(generated.has_value(), "generated terrain compatibility data was missing");
        if (!generated) {
            return;
        }

        Engine::TerrainSettings settings;
        settings.chunkSize = generated->size;
        settings.resolution = generated->resolution;
        settings.createRendererResources = false;
        Engine::TerrainSystem terrain(settings);
        const Engine::TerrainTileHandle tile = terrain.createTileFromHeights(generated->coord, generated->heights);
        ctx.expect(tile.id != UINT32_MAX, "legacy terrain tile creation failed");

        const std::optional<float> datasetHeight = dataset.sampleHeight(1.0f, 1.0f);
        const std::optional<float> terrainHeight = terrain.sampleHeight(1.0f, 1.0f);
        ctx.expect(datasetHeight.has_value() && terrainHeight.has_value(), "compatibility sample was missing");
        if (datasetHeight && terrainHeight) {
            ctx.expect(near(*datasetHeight, *terrainHeight), "legacy TerrainSystem sample did not match dataset sample");
        }
    }

    void noRendererDependencyForDatasetTests(TestContext& ctx)
    {
        Engine::TerrainDataset dataset;
        const Engine::TerrainSourceHandle source = dataset.registerSource(importedSourceDescriptor());
        const Engine::TerrainChunkHandle chunk = dataset.loadImportedChunk(source, importedChunk());
        ctx.expect(Engine::isValid(chunk), "dataset test failed without real renderer linkage");
    }
}

int main()
{
    std::vector<TestFailure> failures;

    const std::vector<std::pair<std::string, void (*)(TestContext&)>> tests = {
        {"RegisterSourceCreatesStableRuntimeHandle", registerSourceCreatesStableRuntimeHandle},
        {"SourceSlotReuseInvalidatesStaleHandle", sourceSlotReuseInvalidatesStaleHandle},
        {"LoadImportedChunkStoresCpuData", loadImportedChunkStoresCpuData},
        {"UnloadChunkInvalidatesHandle", unloadChunkInvalidatesHandle},
        {"UnregisterSourceUnloadsChunks", unregisterSourceUnloadsChunks},
        {"SampleHeightUsesImportedChunkData", sampleHeightUsesImportedChunkData},
        {"RaycastHitsImportedChunk", raycastHitsImportedChunk},
        {"ChunkWorldBoundsAndDiagnostics", chunkWorldBoundsAndDiagnostics},
        {"ProceduralSourceLoadsChunkThroughSameQueryPath", proceduralSourceLoadsChunkThroughSameQueryPath},
        {"ImportedAndProceduralChunksShareRuntimeQueries", importedAndProceduralChunksShareRuntimeQueries},
        {"InvalidInputsAreNoOps", invalidInputsAreNoOps},
        {"DatasetChunkConvertsToGeneratedTerrainTileData", datasetChunkConvertsToGeneratedTerrainTileData},
        {"CompatibilityTerrainSystemMatchesDatasetSampling", compatibilityTerrainSystemMatchesDatasetSampling},
        {"NoRendererDependencyForDatasetTests", noRendererDependencyForDatasetTests},
    };

    for (const auto& [name, test] : tests) {
        TestContext context{name, failures};
        test(context);
    }

    if (!failures.empty()) {
        for (const TestFailure& failure : failures) {
            std::cerr << failure.testName << ": " << failure.message << '\n';
        }
        return 1;
    }

    std::cout << "Terrain dataset tests passed (" << tests.size() << " tests)\n";
    return 0;
}
