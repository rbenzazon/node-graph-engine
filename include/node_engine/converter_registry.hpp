#pragma once

#include "node_engine/type_registry.hpp"
#include "node_engine/value.hpp"

#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace node_engine {

using ConverterFn = std::function<Value(Value const&)>;

struct ConverterDesc {
  std::string name;
  std::string from_key;
  std::string to_key;
  ConverterFn fn;
};

class ConverterRegistry {
 public:
  static ConverterRegistry& instance();

  void add(ConverterDesc desc);
  ConverterDesc const* find(std::string const& from_key, std::string const& to_key) const;
  bool contains(std::string const& from_key, std::string const& to_key) const;
  void ensure_builtins();

 private:
  mutable std::mutex mu_;
  std::unordered_map<std::string, ConverterDesc> by_pair_;
  bool builtins_ready_ = false;

  static std::string pair_key(std::string const& from, std::string const& to) {
    return from + '\0' + to;
  }
};

inline bool types_compatible(std::string const& from_key,
                             std::string const& to_key,
                             TypeId from_type,
                             TypeId to_type) {
  ConverterRegistry::instance().ensure_builtins();
  if (to_type == TypeId::Dynamic || from_type == TypeId::Dynamic) {
    return true;
  }
  if (from_key == to_key) {
    return true;
  }
  return ConverterRegistry::instance().contains(from_key, to_key);
}

inline bool types_compatible(TypeId from, TypeId to) {
  if (from == TypeId::Object || to == TypeId::Object) {
    return from == to;
  }
  return types_compatible(builtin_type_key(from), builtin_type_key(to), from, to);
}

inline Value apply_converter(Value const& v, std::string const& from_key, std::string const& to_key) {
  ConverterRegistry::instance().ensure_builtins();
  if (from_key == to_key) {
    return clone_value(v);
  }
  auto const* c = ConverterRegistry::instance().find(from_key, to_key);
  if (!c) {
    throw std::runtime_error("no converter from " + from_key + " to " + to_key);
  }
  return c->fn(v);
}

template <typename In, typename Out, typename Fn>
void register_converter(std::string name, Fn fn) {
  ConverterDesc desc;
  desc.name = std::move(name);
  desc.from_key = type_key_for<In>();
  desc.to_key = type_key_for<Out>();
  desc.fn = [fn = std::move(fn)](Value const& in_v) -> Value {
    In in_val{};
    if constexpr (is_builtin_pin_v<In>) {
      in_val = std::get<In>(in_v);
    } else {
      in_val = TypeRegistry::instance().cast<In>(std::get<ObjectValue>(in_v));
    }
    Out out_val{};
    fn(in_val, out_val);
    if constexpr (is_builtin_pin_v<Out>) {
      return Value{std::move(out_val)};
    } else {
      return Value{TypeRegistry::instance().make_object_value(std::move(out_val))};
    }
  };
  ConverterRegistry::instance().add(std::move(desc));
}

}  // namespace node_engine
