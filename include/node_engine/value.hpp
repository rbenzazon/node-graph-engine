#pragma once

#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace node_engine {

enum class TypeId {
  Float,
  Bool,
  String,
  FloatBuffer,
  Dynamic,
};

using Value = std::variant<
    std::monostate,
    double,
    bool,
    std::string,
    std::vector<double>>;

inline char const* to_string(TypeId t) {
  switch (t) {
    case TypeId::Float: return "Float";
    case TypeId::Bool: return "Bool";
    case TypeId::String: return "String";
    case TypeId::FloatBuffer: return "FloatBuffer";
    case TypeId::Dynamic: return "Dynamic";
  }
  return "Unknown";
}

inline TypeId type_of(Value const& v) {
  switch (v.index()) {
    case 1: return TypeId::Float;
    case 2: return TypeId::Bool;
    case 3: return TypeId::String;
    case 4: return TypeId::FloatBuffer;
    default: return TypeId::Dynamic;
  }
}

inline bool can_autoconvert(TypeId from, TypeId to) {
  if (from == to || to == TypeId::Dynamic || from == TypeId::Dynamic) {
    return true;
  }
  return (from == TypeId::Bool && to == TypeId::Float) ||
         (from == TypeId::Float && to == TypeId::Bool);
}

inline Value autoconvert(Value const& v, TypeId to) {
  if (to == TypeId::Dynamic) {
    return v;
  }
  TypeId from = type_of(v);
  if (from == to || from == TypeId::Dynamic) {
    return v;
  }
  if (from == TypeId::Bool && to == TypeId::Float) {
    return Value{std::get<bool>(v) ? 1.0 : 0.0};
  }
  if (from == TypeId::Float && to == TypeId::Bool) {
    return Value{std::get<double>(v) != 0.0};
  }
  throw std::runtime_error(std::string("no autoconvert from ") + to_string(from) +
                           " to " + to_string(to));
}

template <typename T>
inline TypeId type_id_for() {
  if constexpr (std::is_same_v<T, double> || std::is_same_v<T, float>) {
    return TypeId::Float;
  } else if constexpr (std::is_same_v<T, bool>) {
    return TypeId::Bool;
  } else if constexpr (std::is_same_v<T, std::string>) {
    return TypeId::String;
  } else if constexpr (std::is_same_v<T, std::vector<double>>) {
    return TypeId::FloatBuffer;
  } else {
    static_assert(sizeof(T) == 0, "unsupported pin type");
  }
}

}  // namespace node_engine
