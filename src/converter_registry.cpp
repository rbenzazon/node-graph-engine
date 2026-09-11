#include "node_engine/converter_registry.hpp"

#include <cstdint>
#include <type_traits>

namespace node_engine {
namespace {

template <typename To>
void add_bool_to(char const* name) {
  register_converter<bool, To>(name, [](bool const& in, To& out) {
    out = in ? static_cast<To>(1) : static_cast<To>(0);
  });
}

template <typename From>
void add_to_bool(char const* name) {
  register_converter<From, bool>(name, [](From const& in, bool& out) {
    if constexpr (std::is_floating_point_v<From>) {
      out = (in != static_cast<From>(0));
    } else {
      out = (in != From{0});
    }
  });
}

template <typename From, typename To>
void add_num(char const* name) {
  register_converter<From, To>(name, [](From const& in, To& out) { out = static_cast<To>(in); });
}

void register_builtin_converters() {
  add_bool_to<float>("builtin.bool_to_float32");
  add_bool_to<double>("builtin.bool_to_float64");
  add_to_bool<float>("builtin.float32_to_bool");
  add_to_bool<double>("builtin.float64_to_bool");

  add_num<float, double>("builtin.float32_to_float64");
  add_num<double, float>("builtin.float64_to_float32");

  add_num<std::int32_t, double>("builtin.int32_to_float64");
  add_num<std::int64_t, double>("builtin.int64_to_float64");
  add_num<std::uint32_t, double>("builtin.uint32_to_float64");
  add_num<std::uint64_t, double>("builtin.uint64_to_float64");
  add_num<std::int16_t, double>("builtin.int16_to_float64");
  add_num<std::uint16_t, double>("builtin.uint16_to_float64");
  add_num<std::int8_t, double>("builtin.int8_to_float64");
  add_num<std::uint8_t, double>("builtin.uint8_to_float64");

  add_num<double, std::int32_t>("builtin.float64_to_int32");
  add_num<double, std::int64_t>("builtin.float64_to_int64");
  add_num<double, std::uint32_t>("builtin.float64_to_uint32");
  add_num<double, std::uint64_t>("builtin.float64_to_uint64");

  add_num<std::int8_t, std::int16_t>("builtin.int8_to_int16");
  add_num<std::int16_t, std::int32_t>("builtin.int16_to_int32");
  add_num<std::int32_t, std::int64_t>("builtin.int32_to_int64");
  add_num<std::uint8_t, std::uint16_t>("builtin.uint8_to_uint16");
  add_num<std::uint16_t, std::uint32_t>("builtin.uint16_to_uint32");
  add_num<std::uint32_t, std::uint64_t>("builtin.uint32_to_uint64");
}

}  // namespace

ConverterRegistry& ConverterRegistry::instance() {
  static ConverterRegistry reg;
  return reg;
}

void ConverterRegistry::add(ConverterDesc desc) {
  ensure_builtins();
  std::lock_guard lock(mu_);
  std::string key = pair_key(desc.from_key, desc.to_key);
  auto it = by_pair_.find(key);
  if (it != by_pair_.end()) {
    if (it->second.name == desc.name) {
      return;
    }
    throw std::runtime_error("duplicate converter for pair " + desc.from_key + " -> " + desc.to_key +
                             " (" + it->second.name + " vs " + desc.name + ")");
  }
  by_pair_.emplace(std::move(key), std::move(desc));
}

ConverterDesc const* ConverterRegistry::find(std::string const& from_key,
                                             std::string const& to_key) const {
  const_cast<ConverterRegistry*>(this)->ensure_builtins();
  std::lock_guard lock(mu_);
  auto it = by_pair_.find(pair_key(from_key, to_key));
  if (it == by_pair_.end()) {
    return nullptr;
  }
  return &it->second;
}

bool ConverterRegistry::contains(std::string const& from_key, std::string const& to_key) const {
  return find(from_key, to_key) != nullptr;
}

void ConverterRegistry::ensure_builtins() {
  {
    std::lock_guard lock(mu_);
    if (builtins_ready_) {
      return;
    }
    builtins_ready_ = true;
  }
  register_builtin_converters();
}

bool can_autoconvert(TypeId from, TypeId to) {
  return types_compatible(from, to);
}

Value autoconvert(Value const& v, TypeId to) {
  if (to == TypeId::Dynamic) {
    return clone_value(v);
  }
  TypeId from = type_of(v);
  if (from == to || from == TypeId::Dynamic) {
    return clone_value(v);
  }
  return apply_converter(v, builtin_type_key(from), builtin_type_key(to));
}

}  // namespace node_engine
