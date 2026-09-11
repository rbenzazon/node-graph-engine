# Type system and direct converters

## Model

- **Bare nodes** declare pins with C++ types via `Ports` (`p.in<T>` / `p.out<T>`).
- **Builtins** live in the closed `Value` variant (`Bool`, `Float32`/`Float64`, ints/uints, `String`, buffers).
- **Project structs** use `TypeId::Object` + a registered string `type_key` (not a mega-union of every app type).
- **Converters** are **direct only**: a registered exact pair `(from_key → to_key)`. No multi-hop path search.
- Conversion runs on **edge copy** in the scheduler (and in `read_pin_as` / `get<T>` when needed). Nodes do not fetch converters.

`Float` / `FloatBuffer` remain aliases of `Float64` / `Float64Buffer` for compatibility. `float` and `double` are distinct types with a builtin converter between them.

## Registering a struct type

```cpp
struct SensorSample { double t_s; double value; };

register_type<SensorSample>("demo.SensorSample");
// or: REGISTER_TYPE(SensorSample, "demo.SensorSample");
```

Use the type on pins:

```cpp
static void ports(Ports& p) {
  p.out<SensorSample>("out");
  p.in<SensorSample>("in");
}
```

`Node::set_out` / `get` box and unbox through `TypeRegistry`.

## Registering a direct converter

```cpp
register_converter<SensorSample, SampleBatch>(
    "demo.sample_to_batch",
    [](SensorSample const& in, SampleBatch& out) {
      out.values = {in.t_s, in.value};
    });
```

Requirements:

- Both endpoint types must already be registered (or builtins).
- Only this exact pair becomes valid; `A→C` is **not** inferred from `A→B` and `B→C`.

## Compatibility and validation

`types_compatible(from_key, to_key, from_type, to_type)` is true when:

1. either side is `Dynamic`, or
2. keys are equal (identity), or
3. a direct converter exists for the pair.

`validate_graph` rejects wires that fail this check.

`can_autoconvert` / `autoconvert` are thin wrappers over the same registry (for older call sites).

## Builtin converters (lean set)

Registered by `ConverterRegistry::ensure_builtins()`:

| Pair | Notes |
|------|--------|
| Bool ↔ Float32 / Float64 | truthy / 0–1 |
| Float32 ↔ Float64 | explicit; not the same type |
| common ints/uints → Float64 | widening to double |
| Float64 → Int32/64, Uint32/64 | truncating cast |
| Int8→16→32→64, Uint8→16→32→64 | same-signed widen chain |

**Not** registered (fail-closed): signed↔unsigned of same width, arbitrary buffer casts, multi-hop chains.

## Runtime edge copy

For each inbound edge, the scheduler:

1. reads the source output `Value`
2. resolves `from_key` / `to_key` from the live value and pin decls
3. if keys differ and a converter exists, applies it
4. otherwise clones (objects deep-clone via `TypeOps::clone`)

Fan-out still deep-copies today; immutable shared inputs are a later Tier 1 item (`todo.md`).

## Headers

| Header | Role |
|--------|------|
| `value.hpp` | `TypeId`, `Value`, `ObjectValue`, keys, `type_id_for` |
| `type_registry.hpp` | struct register / clone / cast |
| `converter_registry.hpp` | direct pair registry + `register_converter` |
| `pin.hpp` / `ports.hpp` | `type_key` on pins and port decls |
| `node.hpp` | `get` / `set_out` for builtins and objects |

## Demo / tests

- Demo: `demos/typed_pins_converter_demo.cpp`
- Smoke: object convert path + reject missing pair + Int32 identity + expanded builtin checks
