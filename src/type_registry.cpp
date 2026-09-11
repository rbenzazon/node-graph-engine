#include "node_engine/type_registry.hpp"

#include <stdexcept>

namespace node_engine {

TypeRegistry& TypeRegistry::instance() {
  static TypeRegistry reg;
  return reg;
}

void TypeRegistry::register_ops(TypeOps ops) {
  std::lock_guard lock(mu_);
  if (by_key_.count(ops.key)) {
    return;
  }
  by_key_.emplace(ops.key, std::move(ops));
}

bool TypeRegistry::contains_key(std::string const& key) const {
  std::lock_guard lock(mu_);
  return by_key_.count(key) != 0;
}

TypeOps const* TypeRegistry::find(std::string const& key) const {
  std::lock_guard lock(mu_);
  auto it = by_key_.find(key);
  if (it == by_key_.end()) {
    return nullptr;
  }
  return &it->second;
}

ObjectValue TypeRegistry::make_object(std::string const& key) const {
  TypeOps const* ops = find(key);
  if (!ops || !ops->make_default) {
    throw std::runtime_error("unknown object type key: " + key);
  }
  return ObjectValue{key, ops->make_default()};
}

ObjectValue TypeRegistry::clone_object(ObjectValue const& src) const {
  if (!src.ptr) {
    return ObjectValue{src.type_key, nullptr};
  }
  TypeOps const* ops = find(src.type_key);
  if (!ops || !ops->clone) {
    throw std::runtime_error("cannot clone object type: " + src.type_key);
  }
  return ObjectValue{src.type_key, ops->clone(src.ptr.get())};
}

Value clone_value(Value const& v) {
  if (auto const* o = std::get_if<ObjectValue>(&v)) {
    return Value{TypeRegistry::instance().clone_object(*o)};
  }
  return v;
}

}  // namespace node_engine
