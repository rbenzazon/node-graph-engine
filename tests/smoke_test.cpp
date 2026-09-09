#include "node_engine/engine.hpp"
#include "node_engine/factory.hpp"
#include "node_engine/graph.hpp"
#include "node_engine/graph_node.hpp"
#include "node_engine/value.hpp"

#include "boundary.hpp"
#include "buffer_to_stream.hpp"
#include "chunk_coalescer.hpp"
#include "examples/arm_then_go.hpp"
#include "examples/producer_consumer.hpp"
#include "examples/scale_filter.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>

namespace {

int fail(char const* msg) {
  std::cerr << "FAIL: " << msg << '\n';
  return EXIT_FAILURE;
}

}  // namespace

int main() {
  using namespace node_engine;
  using namespace node_engine::nodes;

  // Value / autoconvert
  Value v = 1.0;
  if (!std::holds_alternative<double>(v)) {
    return fail("Value variant");
  }
  if (!can_autoconvert(TypeId::Bool, TypeId::Float)) {
    return fail("autoconvert table");
  }
  Value b = autoconvert(Value{true}, TypeId::Float);
  if (std::get<double>(b) != 1.0) {
    return fail("bool->float convert");
  }

  // Factory registration
  auto& factory = NodeFactory::instance();
  if (!factory.contains("scale_filter") || !factory.contains("sequence_producer") ||
      !factory.contains("arm_then_go") || !factory.contains("buffer_to_stream")) {
    return fail("REGISTER_NODE missing types");
  }
  auto n = factory.create("scale_filter");
  if (!n || n->type_id() != "scale_filter") {
    return fail("factory create");
  }

  Engine engine{2};

  // Linear wire + parallel scale
  {
    Graph g;
    g.add_node("p", std::make_unique<SequenceProducer>());
    g.add_node("f", std::make_unique<ScaleFilter>());
    g.add_node("c", std::make_unique<BufferConsumer>());
    g.find_node("p")->find_input("count")->literal = 128.0;
    g.find_node("f")->find_input("gain")->literal = 2.0;
    g.connect("p", "out", "f", "in");
    g.connect("f", "out", "c", "in");

    auto errs = validate_graph(g);
    if (!errs.empty()) {
      return fail(errs.front().message.c_str());
    }

    FlatGraph flat = engine.run_once(g);
    auto* c = dynamic_cast<BufferConsumer*>(flat.find_node("c"));
    if (!c || c->last().size() != 128 || c->last()[3] != 6.0) {
      return fail("linear pipeline values");
    }
  }

  // Two GraphNode instances — independent clone state
  {
    auto bp = std::make_shared<Graph>();
    bp->add_node("in", make_boundary_in(TypeId::FloatBuffer));
    bp->add_node("s", std::make_unique<ScaleFilter>());
    bp->add_node("out", make_boundary_out(TypeId::FloatBuffer));
    bp->find_node("s")->find_input("gain")->literal = 5.0;
    bp->connect("in", "out", "s", "in");
    bp->connect("s", "out", "out", "in");

    Graph outer;
    outer.add_node("src", std::make_unique<SequenceProducer>());
    outer.add_node("g0", std::make_unique<GraphNode>(bp, "g0"));
    outer.add_node("g1", std::make_unique<GraphNode>(bp, "g1"));
    outer.add_node("c0", std::make_unique<BufferConsumer>());
    outer.add_node("c1", std::make_unique<BufferConsumer>());
    outer.find_node("src")->find_input("count")->literal = 4.0;
    outer.connect("src", "out", "g0", "in");
    outer.connect("src", "out", "g1", "in");
    outer.connect("g0", "out", "c0", "in");
    outer.connect("g1", "out", "c1", "in");

    FlatGraph flat = engine.compile(outer);
    // No wrappers/boundaries remain.
    for (auto const& named : flat.nodes) {
      if (named.node->role() == Role::GraphWrapper || named.node->role() == Role::BoundaryIn ||
          named.node->role() == Role::BoundaryOut) {
        return fail("flatten left nesting artifacts");
      }
    }
    // Two independent scale nodes
    int scales = 0;
    for (auto const& named : flat.nodes) {
      if (named.node->type_id() == "scale_filter") {
        ++scales;
      }
    }
    if (scales != 2) {
      return fail("expected 2 cloned scale_filter instances");
    }

    for (auto& named : flat.nodes) {
      named.node->dirty = true;
    }
    engine.tick(flat);
    auto* c0 = dynamic_cast<BufferConsumer*>(flat.find_node("c0"));
    auto* c1 = dynamic_cast<BufferConsumer*>(flat.find_node("c1"));
    if (!c0 || !c1 || c0->last().size() != 4 || c1->last()[1] != 5.0) {
      return fail("nested instance values");
    }
  }

  // BufferToStream sub-ticks
  {
    Graph g;
    g.add_node("p", std::make_unique<SequenceProducer>());
    g.add_node("d", std::make_unique<BufferToStream>());
    g.add_node("c", std::make_unique<ScalarConsumer>());
    g.find_node("p")->find_input("count")->literal = 3.0;
    g.connect("p", "out", "d", "chunk");
    g.connect("d", "sample", "c", "in");
    FlatGraph flat = engine.run_once(g);
    auto* c = dynamic_cast<ScalarConsumer*>(flat.find_node("c"));
    // After full drain, last sample should be 2.
    if (!c || !c->has_value() || c->last() != 2.0) {
      return fail("buffer_to_stream drain");
    }
  }

  // ArmThenGo temporal
  {
    Graph g;
    g.add_node("a", std::make_unique<ArmThenGo>());
    g.add_node("c", std::make_unique<ScalarConsumer>());
    g.connect("a", "value", "c", "in");
    FlatGraph flat = engine.compile(g);
    auto* arm = flat.find_node("a");
    arm->find_input("payload")->literal = 42.0;

    auto tick_with = [&](bool arm_v, bool go_v) {
      arm->find_input("arm")->literal = arm_v;
      arm->find_input("go")->literal = go_v;
      arm->dirty = true;
      engine.tick(flat);
    };

    tick_with(false, false);
    tick_with(true, false);   // arm rising
    tick_with(true, true);    // go rising while armed -> fire
    auto* c = dynamic_cast<ScalarConsumer*>(flat.find_node("c"));
    if (!c || c->last() != 42.0) {
      return fail("arm_then_go fire");
    }
  }

  // Trigger queue
  {
    Graph g;
    g.add_node("p", std::make_unique<SequenceProducer>());
    g.add_node("c", std::make_unique<BufferConsumer>());
    g.find_node("p")->find_input("count")->literal = 2.0;
    g.connect("p", "out", "c", "in");
    FlatGraph flat = engine.compile(g);
    for (auto& n : flat.nodes) {
      n.node->dirty = false;
    }
    engine.triggers().push("p");
    if (!engine.poll_trigger_and_run(flat)) {
      return fail("trigger poll");
    }
    auto* c = dynamic_cast<BufferConsumer*>(flat.find_node("c"));
    if (!c || c->last().size() != 2) {
      return fail("trigger run");
    }
  }

  std::cout << "smoke_test ok\n";
  return EXIT_SUCCESS;
}
