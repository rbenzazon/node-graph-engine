#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace node_engine {

// Builtin pin types. Float/FloatBuffer alias Float64 / Float64Buffer for compat.
enum class TypeId : std::uint16_t {
  Dynamic = 0,
  Bool,
  Float32,
  Float64,
  Int8,
  Int16,
  Int32,
  Int64,
  Uint8,
  Uint16,
  Uint32,
  Uint64,
  String,
  Float32Buffer,
  Float64Buffer,
  Int32Buffer,
  Uint8Buffer,
  Object,
  Float = Float64,
  FloatBuffer = Float64Buffer,
};

struct ObjectValue {
  std::string type_key;
  std::shared_ptr<void> ptr;
  bool empty() const { return !ptr; }
};

using Value = std::variant<
    std::monostate,
    bool,
    float,
    double,
    std::int8_t,
    std::int16_t,
    std::int32_t,
    std::int64_t,
    std::uint8_t,
    std::uint16_t,
    std::uint32_t,
    std::uint64_t,
    std::string,
    std::vector<float>,
    std::vector<double>,
    std::vector<std::int32_t>,
    std::vector<std::uint8_t>,
    ObjectValue>;

inline char const* to_string(TypeId t) {
  switch (t) {
    case TypeId::Dynamic: return "Dynamic";
    case TypeId::Bool: return "Bool";
    case TypeId::Float32: return "Float32";
    case TypeId::Float64: return "Float64";
    case TypeId::Int8: return "Int8";
    case TypeId::Int16: return "Int16";
    case TypeId::Int32: return "Int32";
    case TypeId::Int64: return "Int64";
    case TypeId::Uint8: return "Uint8";
    case TypeId::Uint16: return "Uint16";
    case TypeId::Uint32: return "Uint32";
    case TypeId::Uint64: return "Uint64";
    case TypeId::String: return "String";
    case TypeId::Float32Buffer: return "Float32Buffer";
    case TypeId::Float64Buffer: return "Float64Buffer";
    case TypeId::Int32Buffer: return "Int32Buffer";
    case TypeId::Uint8Buffer: return "Uint8Buffer";
    case TypeId::Object: return "Object";
  }
  return "Unknown";
}

inline std::string builtin_type_key(TypeId t) {
  switch (t) {
    case TypeId::Dynamic: return "core.Dynamic";
    case TypeId::Bool: return "core.Bool";
    case TypeId::Float32: return "core.Float32";
    case TypeId::Float64: return "core.Float64";
    case TypeId::Int8: return "core.Int8";
    case TypeId::Int16: return "core.Int16";
    case TypeId::Int32: return "core.Int32";
    case TypeId::Int64: return "core.Int64";
    case TypeId::Uint8: return "core.Uint8";
    case TypeId::Uint16: return "core.Uint16";
    case TypeId::Uint32: return "core.Uint32";
    case TypeId::Uint64: return "core.Uint64";
    case TypeId::String: return "core.String";
    case TypeId::Float32Buffer: return "core.Float32Buffer";
    case TypeId::Float64Buffer: return "core.Float64Buffer";
    case TypeId::Int32Buffer: return "core.Int32Buffer";
    case TypeId::Uint8Buffer: return "core.Uint8Buffer";
    case TypeId::Object: return "core.Object";
  }
  return "core.Unknown";
}

inline TypeId type_of(Value const& v) {
  switch (v.index()) {
    case 1: return TypeId::Bool;
    case 2: return TypeId::Float32;
    case 3: return TypeId::Float64;
    case 4: return TypeId::Int8;
    case 5: return TypeId::Int16;
    case 6: return TypeId::Int32;
    case 7: return TypeId::Int64;
    case 8: return TypeId::Uint8;
    case 9: return TypeId::Uint16;
    case 10: return TypeId::Uint32;
    case 11: return TypeId::Uint64;
    case 12: return TypeId::String;
    case 13: return TypeId::Float32Buffer;
    case 14: return TypeId::Float64Buffer;
    case 15: return TypeId::Int32Buffer;
    case 16: return TypeId::Uint8Buffer;
    case 17: return TypeId::Object;
    default: return TypeId::Dynamic;
  }
}

inline std::string value_type_key(Value const& v) {
  if (auto const* o = std::get_if<ObjectValue>(&v)) {
    return o->type_key;
  }
  return builtin_type_key(type_of(v));
}

inline std::string pin_type_key(TypeId type, std::string const& object_key) {
  if (type == TypeId::Object) {
    return object_key;
  }
  return builtin_type_key(type);
}

namespace detail {

template <typename T>
struct BuiltinTypeMap;

template <>
struct BuiltinTypeMap<bool> {
  static constexpr TypeId id = TypeId::Bool;
};
template <>
struct BuiltinTypeMap<float> {
  static constexpr TypeId id = TypeId::Float32;
};
template <>
struct BuiltinTypeMap<double> {
  static constexpr TypeId id = TypeId::Float64;
};
template <>
struct BuiltinTypeMap<std::int8_t> {
  static constexpr TypeId id = TypeId::Int8;
};
template <>
struct BuiltinTypeMap<std::int16_t> {
  static constexpr TypeId id = TypeId::Int16;
};
template <>
struct BuiltinTypeMap<std::int32_t> {
  static constexpr TypeId id = TypeId::Int32;
};
template <>
struct BuiltinTypeMap<std::int64_t> {
  static constexpr TypeId id = TypeId::Int64;
};
template <>
struct BuiltinTypeMap<std::uint8_t> {
  static constexpr TypeId id = TypeId::Uint8;
};
template <>
struct BuiltinTypeMap<std::uint16_t> {
  static constexpr TypeId id = TypeId::Uint16;
};
template <>
struct BuiltinTypeMap<std::uint32_t> {
  static constexpr TypeId id = TypeId::Uint32;
};
template <>
struct BuiltinTypeMap<std::uint64_t> {
  static constexpr TypeId id = TypeId::Uint64;
};
template <>
struct BuiltinTypeMap<std::string> {
  static constexpr TypeId id = TypeId::String;
};
template <>
struct BuiltinTypeMap<std::vector<float>> {
  static constexpr TypeId id = TypeId::Float32Buffer;
};
template <>
struct BuiltinTypeMap<std::vector<double>> {
  static constexpr TypeId id = TypeId::Float64Buffer;
};
template <>
struct BuiltinTypeMap<std::vector<std::int32_t>> {
  static constexpr TypeId id = TypeId::Int32Buffer;
};
template <>
struct BuiltinTypeMap<std::vector<std::uint8_t>> {
  static constexpr TypeId id = TypeId::Uint8Buffer;
};

template <typename T>
concept HasBuiltinMap = requires { BuiltinTypeMap<T>::id; };

}  // namespace detail

template <typename T>
inline constexpr bool is_builtin_pin_v = detail::HasBuiltinMap<T>;

template <typename T>
inline TypeId type_id_for() {
  if constexpr (is_builtin_pin_v<T>) {
    return detail::BuiltinTypeMap<T>::id;
  } else {
    return TypeId::Object;
  }
}

Value clone_value(Value const& v);
bool can_autoconvert(TypeId from, TypeId to);
Value autoconvert(Value const& v, TypeId to);

}  // namespace node_engine
