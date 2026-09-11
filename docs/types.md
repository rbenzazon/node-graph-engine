# Pin types (overview)

The engine uses an **open struct + closed builtin** model.

- **Builtins** are the fixed `TypeId` / `Value` alternatives (bool, float32/64, integer widths, string, buffers).
- **Project structs** are `TypeId::Object` values identified by a registered `type_key`.
- **Direct converters** (exact from-to pairs) bridge compatible pins. There is **no multi-hop** conversion.

Full details, registration APIs, builtin converter table, and edge-copy behavior:

See **[type-system-converters.md](type-system-converters.md)**.

## Quick reference

| Kind | Declare | Payload |
|------|---------|---------|
| Float64 (`Float` alias) | `p.in<double>` | `double` |
| Float32 | `p.in<float>` | `float` |
| Bool | `p.in<bool>` | `bool` |
| Int/Uint | `p.in<std::int32_t>` etc. | matching integer |
| String | `p.in<std::string>` | `std::string` |
| Float64 buffer (`FloatBuffer`) | `p.in_buffer` / vector of double | `std::vector<double>` |
| Float32 / bytes buffers | `p.in_buffer_f32` / `p.in_bytes` | matching vectors |
| Struct | `p.in<MyT>` after `register_type` | `ObjectValue` |

## Enforcement

1. **Validate**: `types_compatible` on wire endpoints (keys + direct converter table).
2. **Edge copy**: apply direct converter or clone into the destination pin.
3. **`get<T>()`**: convert/read via the same rules; wrong alternative still throws at `std::get` / cast.

## Compatibility aliases

- `TypeId::Float` == `Float64`
- `TypeId::FloatBuffer` == `Float64Buffer`

Prefer spelling `double` / `Float64` in new code when the distinction from `float` matters.
