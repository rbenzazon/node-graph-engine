#include "node_engine/graph.hpp"

#include "node_engine/converter_registry.hpp"

#include <queue>
#include <stdexcept>
#include <unordered_map>

namespace node_engine {

Node* Graph::find_node(std::string const& id) {
  for (auto& named : nodes) {
    if (named.id == id) {
      return named.node.get();
    }
  }
  return nullptr;
}

Node const* Graph::find_node(std::string const& id) const {
  for (auto const& named : nodes) {
    if (named.id == id) {
      return named.node.get();
    }
  }
  return nullptr;
}

NamedNode* Graph::find_named(std::string const& id) {
  for (auto& named : nodes) {
    if (named.id == id) {
      return &named;
    }
  }
  return nullptr;
}

NamedNode const* Graph::find_named(std::string const& id) const {
  for (auto const& named : nodes) {
    if (named.id == id) {
      return &named;
    }
  }
  return nullptr;
}

void Graph::add_node(std::string id, std::unique_ptr<Node> node) {
  if (find_node(id) != nullptr) {
    throw std::runtime_error("duplicate node id: " + id);
  }
  nodes.push_back(NamedNode{std::move(id), std::move(node)});
}

void Graph::connect(std::string from_node, std::string from_pin, std::string to_node,
                    std::string to_pin) {
  wires.push_back(Wire{
      std::move(from_node),
      std::move(from_pin),
      std::move(to_node),
      std::move(to_pin),
  });
}

Graph Graph::clone() const {
  Graph out;
  out.nodes.reserve(nodes.size());
  for (auto const& named : nodes) {
    if (!named.node) {
      throw std::runtime_error("cannot clone null node: " + named.id);
    }
    out.nodes.push_back(NamedNode{named.id, named.node->clone()});
  }
  out.wires = wires;
  return out;
}

std::vector<ValidationError> validate_graph(Graph const& graph) {
  std::vector<ValidationError> errors;
  std::unordered_map<std::string, Node const*> by_id;
  for (auto const& named : graph.nodes) {
    if (!named.node) {
      errors.push_back(ValidationError{"null node: " + named.id});
      continue;
    }
    if (!by_id.emplace(named.id, named.node.get()).second) {
      errors.push_back(ValidationError{"duplicate node id: " + named.id});
    }
  }

  for (auto const& wire : graph.wires) {
    auto from_it = by_id.find(wire.from_node);
    auto to_it = by_id.find(wire.to_node);
    if (from_it == by_id.end()) {
      errors.push_back(ValidationError{"wire from unknown node: " + wire.from_node});
      continue;
    }
    if (to_it == by_id.end()) {
      errors.push_back(ValidationError{"wire to unknown node: " + wire.to_node});
      continue;
    }
    Pin const* out_pin = from_it->second->find_output(wire.from_pin);
    Pin const* in_pin = to_it->second->find_input(wire.to_pin);
    if (!out_pin) {
      errors.push_back(
          ValidationError{"missing output pin " + wire.from_node + "." + wire.from_pin});
      continue;
    }
    if (!in_pin) {
      errors.push_back(
          ValidationError{"missing input pin " + wire.to_node + "." + wire.to_pin});
      continue;
    }
    {
      std::string from_key = pin_type_key(out_pin->type, out_pin->type_key);
      std::string to_key = pin_type_key(in_pin->type, in_pin->type_key);
      if (!types_compatible(from_key, to_key, out_pin->type, in_pin->type)) {
        errors.push_back(ValidationError{
            "incompatible types on wire " + wire.from_node + "." + wire.from_pin + " -> " +
            wire.to_node + "." + wire.to_pin + " (" + from_key + " -> " + to_key + ")"});
      }
    }
  }

  std::unordered_map<std::string, std::vector<std::string>> adj;
  std::unordered_map<std::string, int> indegree;
  for (auto const& [id, _] : by_id) {
    adj[id];
    indegree[id] = 0;
  }
  for (auto const& wire : graph.wires) {
    if (!by_id.count(wire.from_node) || !by_id.count(wire.to_node)) {
      continue;
    }
    if (wire.from_node == wire.to_node) {
      errors.push_back(ValidationError{"self-cycle on node: " + wire.from_node});
      continue;
    }
    adj[wire.from_node].push_back(wire.to_node);
    indegree[wire.to_node] += 1;
  }

  std::queue<std::string> q;
  for (auto const& [id, deg] : indegree) {
    if (deg == 0) {
      q.push(id);
    }
  }
  std::size_t seen = 0;
  while (!q.empty()) {
    auto id = q.front();
    q.pop();
    ++seen;
    for (auto const& nxt : adj[id]) {
      if (--indegree[nxt] == 0) {
        q.push(nxt);
      }
    }
  }
  if (seen != by_id.size()) {
    errors.push_back(ValidationError{"graph contains a cycle"});
  }

  return errors;
}

void propagate_types(Graph& graph) {
  for (auto& named : graph.nodes) {
    if (named.node) {
      named.node->propagate_types();
    }
  }
}

}  // namespace node_engine
