#include "node_engine/flatten.hpp"
#include "node_engine/graph_node.hpp"

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace node_engine {
namespace {

struct Expanded {
  std::vector<NamedNode> nodes;
  std::vector<Wire> wires;
};

std::string join_id(std::string const& prefix, std::string const& id) {
  if (prefix.empty()) {
    return id;
  }
  return prefix + id;
}

Expanded expand_graph(Graph const& graph, std::string const& prefix) {
  Expanded out;

  struct WrapperInfo {
    std::string flat_id;
    GraphNode const* node = nullptr;
  };
  std::vector<WrapperInfo> wrappers;

  for (auto const& named : graph.nodes) {
    if (!named.node) {
      throw std::runtime_error("null node while flattening: " + named.id);
    }
    std::string flat_id = join_id(prefix, named.id);

    if (named.node->role() == Role::GraphWrapper) {
      auto const* gn = dynamic_cast<GraphNode const*>(named.node.get());
      if (!gn || !gn->blueprint()) {
        throw std::runtime_error("GraphWrapper without blueprint: " + named.id);
      }
      wrappers.push_back(WrapperInfo{flat_id, gn});
      out.nodes.push_back(NamedNode{flat_id, named.node->clone()});
      continue;
    }

    out.nodes.push_back(NamedNode{std::move(flat_id), named.node->clone()});
  }

  for (auto const& wire : graph.wires) {
    out.wires.push_back(Wire{
        join_id(prefix, wire.from_node),
        wire.from_pin,
        join_id(prefix, wire.to_node),
        wire.to_pin,
    });
  }

  for (auto const& w : wrappers) {
    Expanded inner = expand_graph(*w.node->blueprint(), w.flat_id + "/");

    std::unordered_map<std::string, std::string> boundary_in;
    std::unordered_map<std::string, std::string> boundary_out;

    for (auto const& n : inner.nodes) {
      if (!n.node) {
        continue;
      }
      auto slash = n.id.find_last_of('/');
      std::string port = slash == std::string::npos ? n.id : n.id.substr(slash + 1);
      if (n.node->role() == Role::BoundaryIn) {
        boundary_in.emplace(port, n.id);
      } else if (n.node->role() == Role::BoundaryOut) {
        boundary_out.emplace(port, n.id);
      }
    }

    std::vector<Wire> spliced;
    spliced.reserve(out.wires.size() + inner.wires.size());

    auto find_boundary_out_source = [&](std::string const& b_out_id, std::string& src_node,
                                        std::string& src_pin) -> bool {
      for (auto const& iw : inner.wires) {
        if (iw.to_node == b_out_id) {
          src_node = iw.from_node;
          src_pin = iw.from_pin;
          return true;
        }
      }
      return false;
    };

    auto find_boundary_in_sinks = [&](std::string const& b_in_id,
                                     std::vector<std::pair<std::string, std::string>>& sinks) {
      for (auto const& iw : inner.wires) {
        if (iw.from_node == b_in_id) {
          sinks.emplace_back(iw.to_node, iw.to_pin);
        }
      }
    };

    for (auto const& wire : out.wires) {
      bool from_wrap = wire.from_node == w.flat_id;
      bool to_wrap = wire.to_node == w.flat_id;

      if (!from_wrap && !to_wrap) {
        spliced.push_back(wire);
        continue;
      }

      if (to_wrap && !from_wrap) {
        auto it = boundary_in.find(wire.to_pin);
        if (it == boundary_in.end()) {
          throw std::runtime_error("GraphNode missing BoundaryIn port: " + wire.to_pin);
        }
        std::vector<std::pair<std::string, std::string>> sinks;
        find_boundary_in_sinks(it->second, sinks);
        for (auto const& sink : sinks) {
          spliced.push_back(Wire{wire.from_node, wire.from_pin, sink.first, sink.second});
        }
        continue;
      }

      if (from_wrap && !to_wrap) {
        auto it = boundary_out.find(wire.from_pin);
        if (it == boundary_out.end()) {
          throw std::runtime_error("GraphNode missing BoundaryOut port: " + wire.from_pin);
        }
        std::string src_node;
        std::string src_pin;
        if (!find_boundary_out_source(it->second, src_node, src_pin)) {
          throw std::runtime_error("BoundaryOut has no driver: " + wire.from_pin);
        }
        spliced.push_back(Wire{src_node, src_pin, wire.to_node, wire.to_pin});
        continue;
      }

      auto it_out = boundary_out.find(wire.from_pin);
      auto it_in = boundary_in.find(wire.to_pin);
      if (it_out == boundary_out.end() || it_in == boundary_in.end()) {
        throw std::runtime_error("invalid self-wire on GraphNode " + w.flat_id);
      }
      std::string src_node;
      std::string src_pin;
      if (!find_boundary_out_source(it_out->second, src_node, src_pin)) {
        throw std::runtime_error("BoundaryOut has no driver on self-wire");
      }
      std::vector<std::pair<std::string, std::string>> sinks;
      find_boundary_in_sinks(it_in->second, sinks);
      for (auto const& sink : sinks) {
        spliced.push_back(Wire{src_node, src_pin, sink.first, sink.second});
      }
    }

    for (auto const& iw : inner.wires) {
      Node const* from_n = nullptr;
      Node const* to_n = nullptr;
      for (auto const& n : inner.nodes) {
        if (n.id == iw.from_node) {
          from_n = n.node.get();
        }
        if (n.id == iw.to_node) {
          to_n = n.node.get();
        }
      }
      if ((from_n && (from_n->role() == Role::BoundaryIn || from_n->role() == Role::BoundaryOut)) ||
          (to_n && (to_n->role() == Role::BoundaryIn || to_n->role() == Role::BoundaryOut))) {
        continue;
      }
      spliced.push_back(iw);
    }

    std::vector<NamedNode> kept_nodes;
    kept_nodes.reserve(out.nodes.size() + inner.nodes.size());
    for (auto& n : out.nodes) {
      if (n.id == w.flat_id) {
        continue;
      }
      kept_nodes.push_back(std::move(n));
    }
    for (auto& n : inner.nodes) {
      if (!n.node) {
        continue;
      }
      if (n.node->role() == Role::BoundaryIn || n.node->role() == Role::BoundaryOut ||
          n.node->role() == Role::GraphWrapper) {
        continue;
      }
      kept_nodes.push_back(std::move(n));
    }

    out.nodes = std::move(kept_nodes);
    out.wires = std::move(spliced);
  }

  return out;
}

}  // namespace

Node* FlatGraph::find_node(std::string const& id) {
  for (auto& named : nodes) {
    if (named.id == id) {
      return named.node.get();
    }
  }
  return nullptr;
}

Node const* FlatGraph::find_node(std::string const& id) const {
  for (auto const& named : nodes) {
    if (named.id == id) {
      return named.node.get();
    }
  }
  return nullptr;
}

NamedNode* FlatGraph::find_named(std::string const& id) {
  for (auto& named : nodes) {
    if (named.id == id) {
      return &named;
    }
  }
  return nullptr;
}

FlatGraph compile_flat(Graph const& authoring) {
  Expanded expanded = expand_graph(authoring, "");

  FlatGraph flat;
  flat.nodes.reserve(expanded.nodes.size());
  for (auto& n : expanded.nodes) {
    if (!n.node) {
      continue;
    }
    if (n.node->role() == Role::BoundaryIn || n.node->role() == Role::BoundaryOut ||
        n.node->role() == Role::GraphWrapper) {
      continue;
    }
    flat.nodes.push_back(std::move(n));
  }

  std::unordered_set<std::string> live;
  for (auto const& n : flat.nodes) {
    live.insert(n.id);
  }

  for (auto const& w : expanded.wires) {
    if (!live.count(w.from_node) || !live.count(w.to_node)) {
      continue;
    }
    flat.edges.push_back(FlatEdge{w.from_node, w.from_pin, w.to_node, w.to_pin});
  }

  for (auto const& e : flat.edges) {
    Node* to = flat.find_node(e.to_node);
    if (!to) {
      continue;
    }
    if (Pin* pin = to->find_input(e.to_pin)) {
      pin->connected = true;
    }
  }

  return flat;
}

}  // namespace node_engine
