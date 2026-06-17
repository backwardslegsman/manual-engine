# System-Owned Inboxes And Event Sinks

ManualEngine should prefer system-owned inboxes with explicit publish-only sinks over a global event bus registry.

This is an architectural direction for future cross-system messaging. The current `Engine::EventQueue` remains a simple per-frame queue for input and interaction events, but new message-driven systems should follow the rules below unless a concrete subsystem need justifies a narrower direct API.

## Core Model

- A system owns the inbox for the messages it consumes.
- Producers do not own, drain, clear, or subscribe to another system's inbox.
- Producers may publish only through a narrow sink/reference that composition code explicitly gives them.
- The engine/App frame loop owns flush order. Systems flush their inboxes at documented frame phases.
- A sink is an input contract, not a service locator. Publishing to a sink should not imply access to the receiving system's mutable internals.

In code, this should look more like a typed inbox/sink pair than a dynamic global bus:

```cpp
template <typename Event>
class EventSink {
public:
    virtual void publish(Event event) = 0;
    virtual ~EventSink() = default;
};

class NavigationSystem {
public:
    EventSink<NavigationRequest>& requestSink();
    void flushRequests(...);

private:
    EventInbox<NavigationRequest> requests_;
};
```

Composition code wires dependencies:

```cpp
InteractionSystem interactions;
NavigationSystem navigation;

interactions.setNavigationRequestSink(&navigation.requestSink());
```

## Rules

- Inboxes are typed. Avoid `std::any`, unstructured string event names, and generic payload maps for engine behavior.
- Expose publish-only interfaces to producers. Keep draining, clearing, inspection, and subscription APIs private to the owning system unless there is a documented reason.
- Keep event payloads value-like: stable IDs, handles, coordinates, settings snapshots, and small command data. Avoid raw owning pointers or references to mutable internal storage.
- Delivery is deferred by default. Publishing appends to an inbox, and the owning system processes messages only when its flush method is called.
- Flush order must be explicit in the frame loop or a documented scheduler phase. Do not rely on incidental construction order or subscription order.
- Publishing during a flush must have defined behavior. Prefer queuing newly published messages for the next flush unless a system documents bounded same-flush processing.
- Inbox flushes should be budgetable when they can process many messages or touch many chunks, objects, renderer resources, cache records, or navigation records.
- Inboxes are not for synchronous queries. Use direct methods for immediate reads such as `sampleHeight`, `findObject`, or renderer handle lookup.
- Inboxes are not a replacement for clear ownership. If only one caller ever needs a tightly coupled operation, a direct method can be the better contract.
- Avoid a global registry of dynamically created buses. If runtime discovery is ever needed, it must have a documented owner, type contract, lifetime policy, and shutdown order.

## Lifetime And Ownership

- The receiving system owns the inbox lifetime.
- A sink reference must not outlive the owning system.
- Prefer passing sinks during explicit composition/startup and clearing them during shutdown or test teardown.
- If long-lived objects cache sink pointers, they must tolerate the sink being absent during teardown and tests.
- Destroying a system destroys its inbox and any queued messages. No other system should assume pending messages survive receiver shutdown.

## Frame Phases

The current App-owned frame loop can remain explicit. As systems move toward inboxes, use broad phases rather than ad hoc dispatch:

1. Platform/input collection.
2. Input mapping and command publication.
3. Interaction and selection command flush.
4. Simulation command flush.
5. Fixed-step simulation.
6. Derived-data scheduling and budgeted main-thread work.
7. Renderer synchronization.
8. Debug UI and diagnostics.
9. End-of-frame queue clearing.

The exact order can evolve, but each inbox-owning system should document when its inbox is flushed and what state may be mutated during that flush.

## Threading

- Normal gameplay inboxes are main-thread structures unless explicitly documented otherwise.
- Worker jobs may produce plain result data and publish/hand it back through a main-thread merge point.
- A sink that can be called from worker threads must say so in its public contract and must define synchronization, cancellation, and shutdown behavior.
- Renderer/bgfx resource mutation, live `World` mutation, live `TerrainSystem` storage, live `NavigationSystem` tile insertion, and `SpatialRegistry` mutation remain main-thread work.

## Debugging And Tests

- Typed sinks should be easy to replace with fake sinks in tests.
- Important inboxes should expose debug counters through the owning system, such as queued, flushed, deferred, dropped, failed, and last message/status.
- Message handling should return or record clear failure states instead of silently dropping invalid commands.
- Avoid hidden global state in tests. A test should be able to instantiate a system, publish to its sink, flush, and assert the outcome.

## Relationship To Current EventQueue

`Engine::EventQueue` is currently a frame-local queue for semantic input and interaction events. It is not a general pub/sub bus. Future inbox work may replace or split it, but that should be done incrementally:

- Keep input mapping as the producer of semantic input events.
- Keep input consumers explicit.
- Move toward narrower typed sinks when a system needs durable command ownership, deferred flushing, budgeted handling, or testable cross-system publication.
