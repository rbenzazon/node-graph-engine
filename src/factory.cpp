#include "node_engine/factory.hpp"

#include <stdexcept>

namespace node_engine {

NodeFactory& NodeFactory::instance() {
  static NodeFactory factory;
  return factory;
}

void NodeFactory::register_type(std::string type_id, NodeCreator creator) {
  creators_[std::move(type_id)] = std::move(creator);
}

std::unique_ptr<Node> NodeFactory::create(std::string_view type_id) const {
  auto const it = creators_.find(std::string{type_id});
  if (it == creators_.end()) {
    throw std::runtime_error("unknown node type: " + std::string{type_id});
  }
  return it->second();
}

std::vector<std::string> NodeFactory::registered_types() const {
  std::vector<std::string> out;
  out.reserve(creators_.size());
  for (auto const& [id, _] : creators_) {
    out.push_back(id);
  }
  return out;
}

}  // namespace node_engine
