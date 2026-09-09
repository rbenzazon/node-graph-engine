#pragma once

#include "node_engine/node.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace node_engine {

using NodeCreator = std::function<std::unique_ptr<Node>()>;

class NodeFactory {
 public:
  static NodeFactory& instance();

  void register_type(std::string type_id, NodeCreator creator);
  [[nodiscard]] std::unique_ptr<Node> create(std::string_view type_id) const;
  [[nodiscard]] bool contains(std::string_view type_id) const;
  [[nodiscard]] std::vector<std::string> registered_types() const;

 private:
  std::unordered_map<std::string, NodeCreator> creators_;
};

struct NodeRegistrar {
  NodeRegistrar(std::string type_id, NodeCreator creator) {
    NodeFactory::instance().register_type(std::move(type_id), std::move(creator));
  }
};

}  // namespace node_engine

#define REGISTER_NODE(Type)                                    \
  static ::node_engine::NodeRegistrar g_node_registrar_##Type{ \
      std::string{Type::meta.type_id},                         \
      []() -> std::unique_ptr<::node_engine::Node> {           \
        return std::make_unique<Type>();                       \
      }}
