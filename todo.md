# Todo

## Later — runtime / fan-out (after typed pins + direct converters)

### Tier 1 — Immutable inputs contract + API (cheap wins)

Defer until the typing/converter plan is done. Does **not** remove edge copies yet; reduces accidental copies and prepares shared/COW fan-out later.

- [ ] Document engine-wide rule: **never mutate input pin buffers**; nodes only write outputs (`set_out` / `buffer_out`).
- [ ] Prefer large-payload access via `const&` (`buffer_in` / future `get_ref<T>`); avoid `get<T>()` **by value** on big buffers/structs in hot paths.
- [ ] Optional debug asserts (or similar) if input buffers are written after connect/copy.
- [ ] Note in docs: Tier 2 (shared immutable / COW values on fan-out) is a separate follow-up when profiling shows copy cost; node “pure/non-mutating” flags are **not** the primary sharing mechanism.

**Context:** Producer → N consumers currently deep-copies `Value` per consumer task (`copy_inbound_edges`). Specs already say read-only fan-out; Tier 1 locks the author/API contract before optimizing representation.
