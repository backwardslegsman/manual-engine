# Assets Guide

`src/Assets` decodes files into engine-friendly CPU-side data.

- Asset importers should not create bgfx resources or depend on SDL window state.
- Return structured results with explicit errors. Avoid logging-only failure paths in reusable importer code.
- Preserve source paths needed by downstream systems, but keep renderer-specific path resolution out of importers.
- Reject unsupported content deliberately at the appropriate runtime boundary. General authored import may preserve skeletal and animated data as CPU records, while static authored runtime loaders should clearly reject assets that require the animated model path.
- Add new import formats behind narrow functions or classes that return common engine data shapes.
