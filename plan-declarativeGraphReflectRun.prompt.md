## Plan: Declarative Graph Reflect + Run + Future Serialize

TL;DR — The repo **already builds graphs in memory**. The goal is not a new “session” world or a webserver. Make the **existing authoring model declarative enough to reflect**, keep models **cleanly serializable later**, and expose a **mid-level API to validate / compile / run (make alive)** a graph. **Edge I/O nodes** (IPC, network, storage, PLC, …) need **declarative config** as pin literals or bind records expressed as **strings / paths / handle tokens**, not C++ pointers stuffed into node members after `dynamic_cast`. JSON can stay a **later addon**; first ship reflection + run + document-shaped IR in C++.

**What this is**
- In-process API over the **current** `Graph` / `Engine` / factory model.
- Reflection of node types, pins (`type` + `type_key`), wires, literals, catalogs.
- Run lifecycle: validate → compile (`FlatGraph`) → bind edges → tick / trigger loop.
- Models chosen so `GraphDocument` (and JSON later) is a mechanical dump of authoring state.

**What this is not**
- Not a web/HTTP session object.
- Not replacing in-memory graph construction.
- Not requiring JSON in core for v1 (IR + reflect first; JSON optional follow-up).
- Not putting sockets/FILE* inside the saved graph — only **declarative bind specs**; runtime resolves them.

**User intent (clarified)**
1. Graphs in memory are native — enhance **declarativeness + reflection**.
2. Models should be **serializable in the future** (stable ids, type_keys, literals, no hidden ctor state).
3. API must **run / keep a graph alive** (compile + tick), not only describe structure.
4. **Edge nodes** need params for real I/O; many values will arrive as **strings or handle tokens** vs pointers/refs used in today’s harnesses (e.g. `FrameEdgeSource::set_emit`).

---

### Current baseline (keep)

| Piece | Role today |
|--------|------------|
| `Graph` + `Wire` + pin literals | Authoring SOT; mostly declarative already |
| `NodeFactory` / `REGISTER_NODE` | Type identity by string `type_id` |
| Typed pins / `Object` + `type_key` / converters | Data model; see type-system-converters.md |
| `Engine::compile` / `tick` / triggers | Makes a flat graph alive |
| Nested `GraphNode` + flatten | Compile-time only |

**Non-declarative gaps (the real delta)**
- Harness injects runtime data via **C++ APIs + `dynamic_cast`** (e.g. `FrameEdgeSource::set_emit`, sink counters) — not pins, not reflectable, not serializable.
- No remove/disconnect; limited edit surface beyond `add_node` / `connect` / poke `pin.literal`.
- No first-class **describe** / catalog beyond factory type list + manual pin walk.
- No `GraphDocument`; Object types lack document codecs.
- Edge “PLC/MES/HMI” nodes are **fake sinks** with bool/double pins only — no endpoint URL, path, channel, credentials-as-config pattern yet.
- Factory `create()` is default-ctor only — fine if **all** config is pins/literals after create.

---

### Target architecture

```text
Catalogs (process)          Authoring (in memory)           Runtime (derived)
NodeFactory                 Graph: nodes, wires, literals   FlatGraph
TypeRegistry + codecs       optional Blueprint library      Engine tick / triggers
ConverterRegistry           EdgeBindSpec (declarative)      resolved I/O resources
        \                        |                                /
         \                  reflect / edit / document IR         /
          \                      |                              /
           ---- mid API: describe, bind_edges, compile, run ----
```

**Declarative rule:** anything that must round-trip or be edited externally lives as:
- node `type_id` + instance `id`
- wires by string endpoints
- **input pin literals** (everything-is-a-pin), including stringly edge config
- optional **edge bind table** keyed by node id (still data, not pointers)
- blueprint refs for `GraphNode`

**Imperative allowed only at process boundary:** open file/socket from path string, map handle token → OS resource, register types/converters, own `Engine` thread pool.

---

### Steps

#### Phase A — Declarative completeness on `Graph`
1. Mutation on existing in-memory `Graph`: `remove_node`, `disconnect*`, validating `try_connect` via `types_compatible` (keys/converters).
2. Literal API: `set_input_literal(graph, node, pin, Value)` with **exact type_key** match (v1).
3. Convention/docs: **no required post-create C++ member config** for nodes that should be reflectable/serializable; use input pins (and defaults in `ports()`).
4. Flag/document anti-patterns: `set_emit`-style members are **test harness only** unless dual-pathed to pins.

#### Phase B — Reflection (make the model inspectable)
5. DTOs: `PinDesc` (**type + type_key** + literal), `NodeDesc`, `WireDesc`, `GraphDesc`, `NodeTypeInfo`, `DataTypeInfo`, `ConverterInfo`.
6. APIs (free functions fine): `describe_graph(Graph const&)`, `list_node_types()`, `list_data_types()`, `list_converters()` — add registry enumerate if missing.
7. Reflection must be rich enough that a host can rebuild the same graph via factory + literals + connects **without** private headers.

#### Phase C — Run API (“make the graph alive”)
8. Mid-level **`GraphRuntime`** (name flexible; **not** Session): host-owned pairing of
   - authoring `Graph&` (or owned copy)
   - `Engine` (or shared executor)
   - compiled `FlatGraph`
   - bind/resolution state
9. Lifecycle ops:
   - `validate()` → structured errors
   - `compile()` → `Engine::compile` / `compile_flat`
   - `bind_edges(EdgeBindings const&)` or apply bind pins — **before or after compile**; define order (recommend: set authoring literals → compile → optional flat pin poke for pure runtime feeds)
   - `tick()` / `run_for` / `poll_trigger_and_run` — keep graph alive
   - `mark_dirty` / push trigger by node id
   - `read_pin` / describe runtime outputs for sinks (reflect live values where serializable)
10. Topology edit while alive: invalidate flat, recompile; document that private node state resets.
11. Multiple graphs: host holds N× `(Graph, GraphRuntime)` — no global session.

#### Phase D — Edge nodes: declarative params (strings / handle tokens)
12. **Problem:** real edge drivers need endpoint config; today either missing or imperative (`set_emit` with `vector<double>` in memory).
13. **Pattern (everything-is-a-pin + bind layer):**
    - **Config pins** (serializable literals), examples:
      - `endpoint` / `uri` : String (`"tcp://…"`, `"ipc://…"`, `"file:…"`, topic name)
      - `path` : String (storage path)
      - `channel` / `topic` / `address` : String or int
      - `mode`, timeouts, batch sizes : builtins
      - `handle_id` : String token meaning “use resource from host bind table”
    - **`EdgeBindings` map** (process-local, **not** necessarily in GraphDocument v1, or stored as string tokens only):
      - `node_id → { resource_kind, token or path, optional extras }`
      - Host resolves token → `shared_ptr<Resource>`, fd, connection — **pointers never enter the graph IR**
14. Split responsibilities:
    - **Graph / document:** declarative strings & scalars on pins (and optional bind token strings).
    - **Host runtime:** `ResourceResolver` opens/looks up real IPC/network/storage objects and attaches them to edge node instances **by node id** after `factory.create`, via a small **`EdgeNode` attach interface** (see below).
15. **`IEdgeBinding` / optional node interface** (minimal, not a deep hierarchy):
    - e.g. virtual `bool attach(EdgeResourceBag const&)` or registry of attach functions by `type_id`
    - Keeps pure compute nodes free of I/O
    - Line-quality fake edges can later take `path`/`uri` no-ops or still accept buffer pins for tests
16. **Harness dual-path for FrameEdgeSource-like producers (v1 design choice):**
    - Prefer feed via **output buffer pin** set on authoring/flat before tick, **or**
    - String pin `source=inline|file|bind` + path/token; file/bind resolved by host
    - Deprecate *necessity* of `dynamic_cast` + `set_emit` for external control planes
17. Security note for later JSON/hosts: treat path/uri literals as untrusted input; resolution policy is host’s job.

#### Phase E — Serializable model now (JSON later)
18. Introduce **`GraphDocument` IR in core** (C++ structs) even if JSON addon waits:
    - format_version, nodes, wires, literals as ValueDoc tagged by **type_key**, blueprint_ref, optional required_type_keys
    - optional `bindings`: node_id → stringly bind spec (no pointers)
19. `document_from_graph` / `graph_from_document` prove serializability; roundtrip tests **are** the serializability guarantee.
20. Object literals: optional **TypeCodec** on TypeRegistry; without codec, topology + type_key only.
21. Wires: endpoints only; converters from process registry at validate.
22. Explicitly **out of document:** `Value` buffers used as live streams, dirty, triggers, OS handles, `shared_ptr` resources, node private members.
23. JSON addon: mechanical map of GraphDocument; not required to land same PR as reflect/run.

#### Phase F — Type registry support
24. `registered_keys()`, converter `list()`, TypeCodec hooks, DocValue AST for codecs (core, JSON-agnostic).

#### Phase G — Tests / demo / docs
25. Tests: reflect rebuild equality; mutate+validate; document RT; runtime compile+tick via GraphRuntime; bind table with fake resolver (string token → in-memory resource); Object±codec; missing type_key fails load.
26. Demo: declarative graph (pins only) → describe → document → reload → bind → run alive — **no** `dynamic_cast` feed path required for the happy path.
27. Docs: `docs/graph-edit-api.md` (reflect, run lifecycle, edge bind pattern, serializability rules); link type-system-converters.md; call out boundary nodes (nesting) vs **system edge** I/O nodes (Role Producer/Consumer at graph boundary).

---

### Relevant files
- include/node_engine/graph.hpp, src/graph.cpp — mutate + validate
- include/node_engine/engine.hpp, scheduler, flatten — run alive
- include/node_engine/factory.hpp — create by type_id
- include/node_engine/value.hpp, pin.hpp, ports.hpp, type_registry, converter_registry — declarative typed model
- include/node_engine/graph_node.hpp, nodes/boundary.hpp — nesting boundaries (not I/O edges)
- nodes/line_quality/frame_edge_source.hpp — imperative set_emit gap
- nodes/line_quality/edge_sinks.hpp — fake edges; future string config pattern
- demos/* — programmatic build patterns to mirror in reflect/rebuild tests
- New: reflect.hpp, runtime.hpp (GraphRuntime), document.hpp, edge_bind.hpp; optional json later

---

### Verification
1. Rebuild a demo graph **only** from describe/factory/literals/connect — same validate + tick result.
2. GraphRuntime: compile + N ticks + trigger path without private node APIs for config.
3. Fake EdgeBindings: token `"mem:frames1"` resolves to buffer feed without storing pointers in Graph.
4. document_from_graph ↔ graph_from_document equality on ids/type_ids/wires/literals/type_keys.
5. PinDesc shows Object type_keys; incompatible wire still rejected.
6. Core build without JSON dependency.

---

### Decisions
- **In-memory Graph remains primary**; work is declarativeness, reflection, run façade, serializable IR.
- **Serializable ⇒ no essential pointer/ref config** in authoring model; strings/tokens + host resolver for edge I/O.
- **Everything-is-a-pin** stays for params; edge attach is a thin host-side bind step, not a second param system.
- **Run API** = validate/compile/bind/tick on host-owned graphs (GraphRuntime), not a web session.
- **JSON** = future/optional encoding of GraphDocument.
- **BoundaryIn/Out** stay nesting-only; **edge** = external I/O producers/consumers.

---

### Further Considerations
1. **Edge attach mechanism** — (A) optional `attach` virtual on edge nodes, (B) external `type_id → attach fn` registry (keeps nodes freer). Recommend **B** for less core hierarchy.
2. **Bindings in GraphDocument v1** — include stringly bind specs vs host-only overrides file. Recommend optional `bindings` section with tokens/paths only.
3. **Live stream feeds** — large buffers as literals vs bind-only at run. Recommend **bind/run path** for bulk data; literals for small config.
4. **GraphEditor vs GraphRuntime** — single façade with describe+edit+run is fine if naming stresses **runtime over live Graph**, not “session.”
