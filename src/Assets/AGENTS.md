# Assets Guide

`src/Assets` decodes files into engine-friendly CPU-side data.

- Asset importers should not create bgfx resources or depend on SDL window state.
- Return structured results with explicit errors. Avoid logging-only failure paths in reusable importer code.
- Preserve source paths needed by downstream systems, but keep renderer-specific path resolution out of importers.
- Reject unsupported content deliberately. For now, static mesh importers should reject skeletal or animated content instead of partially importing it.
- Add new import formats behind narrow functions or classes that return common engine data shapes.
