#pragma once

#include "node_engine/value.hpp"

#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>

namespace node_engine {

struct TypeOps {
  std::string key;
  std::function<std::shared_ptr<void>()> make_default;
  std::function<std::shared_ptr<void>(void const*)> clone;
};

class TypeRegistry {
 public:
  static TypeRegistry& instance();

  void register_ops(TypeOps ops);

  template <typename T>
  void register_type(std::string key) {
    TypeOps ops;
    ops.key = key;
    ops.make_default = []() -> std::shared_ptr<void> {
      return std::shared_ptr<void>(new T(), [](void* p) { delete static_cast<T*>(p); });
    };
    ops.clone = [](void const* src) -> std::shared_ptr<void> {
      return std::shared_ptr<void>(new T(*static_cast<T const*>(src)),
                                  [](void* p) { delete static_cast<T*>(p); });
    };
    std::type_index idx{typeid(T)};
    std::lock_guard lock(mu_);
    auto it = by_index_.find(idx);
    if (it != by_index_.end() && it->second != ops.key) {
      throw std::runtime_error("type already registered under a different key: " + ops.key);
    }
    auto kit = by_key_.find(ops.key);
    if (kit != by_key_.end()) {
      // Idempotent same key.
      by_index_[idx] = ops.key;
      return;
    }
    by_index_[idx] = ops.key;
    by_key_.emplace(ops.key, std::move(ops));
  }

  bool contains_key(std::string const& key) const;
  TypeOps const* find(std::string const& key) const;

  template <typename T>
  std::string key_for() const {
    std::lock_guard lock(mu_);
    auto it = by_index_.find(std::type_index{typeid(T)});
    if (it == by_index_.end()) {
      throw std::runtime_error(std::string("type not registered: ") + typeid(T).name());
    }
    // Return a copy via by_key stable string — store keys in index map value.
    return it->second;
  }

  template <typename T>
  bool is_registered() const {
    std::lock_guard lock(mu_);
    return by_index_.find(std::type_index{typeid(T)}) != by_index_.end();
  }

  ObjectValue make_object(std::string const& key) const;
  ObjectValue clone_object(ObjectValue const& src) const;

  template <typename T>
  ObjectValue make_object_value(T value) const {
    std::string key = key_for<T>();
    auto ptr =
        std::shared_ptr<void>(new T(std::move(value)), [](void* p) { delete static_cast<T*>(p); });
    return ObjectValue{std::move(key), std::move(ptr)};
  }

  template <typename T>
  T const& cast(ObjectValue const& o) const {
    std::string expect = key_for<T>();
    if (o.type_key != expect) {
      throw std::runtime_error("object type mismatch: expected " + expect + " got " + o.type_key);
    }
    if (!o.ptr) {
      throw std::runtime_error("empty object value for " + o.type_key);
    }
    return *static_cast<T const*>(o.ptr.get());
  }

 private:
  mutable std::mutex mu_;
  std::unordered_map<std::string, TypeOps> by_key_;
  std::unordered_map<std::type_index, std::string> by_index_;
};

template <typename T>
inline void register_type(std::string key) {
  TypeRegistry::instance().register_type<T>(std::move(key));
}

template <typename T>
inline std::string type_key_for() {
  if constexpr (is_builtin_pin_v<T>) {
    return builtin_type_key(type_id_for<T>());
  } else {
    return TypeRegistry::instance().key_for<T>();
  }
}

template <typename T>
struct TypeRegistrar {
  explicit TypeRegistrar(std::string key) { register_type<T>(std::move(key)); }
};

#define REGISTER_TYPE(Type, KeyLiteral) \
  static ::node_engine::TypeRegistrar<Type> g_type_registrar_##Type{KeyLiteral}

}  // namespace node_engine
