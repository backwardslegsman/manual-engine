# Physics Guide

`src/Engine/Physics` is reserved for future Jolt-backed physics integration.

- Keep Jolt types private to implementation files where practical. Public headers should expose Engine handles, descriptors, and query results.
- Physics handles are distinct from scene actor/component handles and renderer handles.
- Define transform synchronization rules before adding dynamic bodies.
- Queries should be explicit APIs: raycast, sweep, overlap, closest point.
- Debug draw should emit through the shared debug visualization path once it exists.
- Do not let physics own scene lifetime. Scene components own the intent; physics owns simulation resources.
