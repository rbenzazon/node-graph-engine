Pins are a **closed** type system. A node cannot declare “this pin is my struct.” It can only pick one of five `TypeId`s, and the payload must be one of the alternatives in `Value`.

**Allowed types**

| `TypeId` | `Value` alternative | Declared as |
|---|---|---|
| `Float` | `double` | `p.in<double>("x")` / `p.in<float>("x")` |
| `Bool` | `bool` | `p.in<bool>("ok")` |
| `String` | `std::string` | `p.in<std::string>("s")` |
| `FloatBuffer` | `std::vector<double>` | `p.in_buffer("in")` or `p.in<std::vector<double>>("in")` |
| `Dynamic` | `std::monostate` / unknown | default on undeclared pins; not something `ports()` exposes as a C++ type |

`type_id_for<T>()` and `Node::get<T>()` **do not compile** for any other `T`. There is no user-struct slot in the variant.

**How a node specifies a pin type**

In `static void ports(Ports& p)`:

```cpp
p.in<double>("gain").literal(1.0);
p.in_buffer("in");
p.out<bool>("go");
```

`NodeBase` calls that, then `apply_ports` copies each `PortDecl.type` onto `Pin.type`. The literal is the unwired default. That is the whole declaration API.

**How the engine enforces it**

Three layers, all based on `TypeId`, not C++ structs:

1. **Graph validate** (`validate_graph`): a wire is an error if `!can_autoconvert(out.type, in.type)` and neither pin is `Dynamic`. Same type is fine. `Dynamic` on either end is treated as compatible.

2. **Runtime edge copy** (scheduler): if the live value’s type ≠ the destination pin type, and the pair is in the autoconvert table, the value is converted before it is written into the input pin. Otherwise it is copied as-is.

3. **`get<T>()`**: `read_pin_as` tries the same table. If conversion is allowed, you get the converted value. If not, the raw `Value` is returned and `std::get<T>` throws at runtime if the alternative is wrong.

`propagate_types()` exists and the engine calls it, but the default is empty. Nothing in the catalog fills types from neighbors. Enforcement is the pin’s static `TypeId` plus the global convert table.

**Smart conversion today**

The table is hardcoded in `can_autoconvert` / `autoconvert` in `value.hpp`:

- identity, or either side `Dynamic` → pass through  
- `Bool` → `Float` (`true`→`1.0`, `false`→`0.0`)  
- `Float` → `Bool` (`!= 0.0`)  
- everything else → no convert (`autoconvert` throws if forced)

The spec’s rule for the rest is: insert a **converter node**, not a new implicit cast.

**Extending conversion**

There is **no** registration API and **no** per-node / per-pin convert policy. Conversion is always “global table, applied automatically when `from`/`to` match an entry.”

To add a new implicit cast (e.g. `Float` → `String`) you would edit those two functions (and keep `TypeId`/`Value`/`type_id_for`/`get`/`set_out` in sync). You cannot say “this pin uses conversion X, that pin does not.” A pin only declares its target `TypeId`; if the incoming type is convertible in the global table, the engine always applies that conversion.

**Custom structs**

Not representable as a pin type. Current nodes (e.g. `FusionGo`) explode a logical record into many `double`/`bool` pins. The other intended path is an explicit converter node that reads one closed type and writes another. `Dynamic` only disables checking; it does not let you smuggle a struct through `Value`.