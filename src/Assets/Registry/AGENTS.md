# Asset Registry Guide

`src/Assets/Registry` is reserved for future asset handles, metadata, import records, and dependency tracking.

- Keep importers CPU-only. Registry records should describe source identity, derived data, dependencies, diagnostics, and cache identity, not renderer handles.
- Distinguish source assets, imported records, runtime assets, and renderer resources.
- Asset handles must be stable enough for scene/component references, but renderer resources may still be transient and explicitly owned.
- Include source format, canonical path, source hash, importer version, material pipeline version, texture policy version, and relevant runtime format versions in cache identity.
- Heavy optional assets should only participate in tests when an explicit opt-in or change-specific validation requires them.
