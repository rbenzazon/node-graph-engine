#include "node_engine/engine.hpp"
#include "node_engine/factory.hpp"
#include "node_engine/graph.hpp"
#include "node_engine/graph_node.hpp"

#include "boundary.hpp"
#include "examples/producer_consumer.hpp"
#include "examples/scale_filter.hpp"

#include <iostream>
#include <memory>

int main() {
  using namespace node_engine;
  using namespace node_engine::nodes;

  std::cout << "node_engine pipeline demo\n";

  // producer -> scale_filter -> consumer
  Graph g;
  g.add_node("producer", std::make_unique<SequenceProducer>());
  g.add_node("filter", std::make_unique<ScaleFilter>());
  g.add_node("consumer", std::make_unique<BufferConsumer>());

  // count=8, gain=2
  if (auto* p = g.find_node("producer")) {
    if (Pin* pin = p->find_input("count")) {
      pin->literal = 8.0;
    }
  }
  if (auto* f = g.find_node("filter")) {
    if (Pin* pin = f->find_input("gain")) {
      pin->literal = 2.0;
    }
  }

  g.connect("producer", "out", "filter", "in");
  g.connect("filter", "out", "consumer", "in");

  Engine engine{2};
  FlatGraph flat = engine.run_once(g);

  auto* cons = dynamic_cast<BufferConsumer*>(flat.find_node("consumer"));
  if (!cons) {
    std::cerr << "consumer missing after flatten\n";
    return 1;
  }
  auto const& last = cons->last();
  std::cout << "consumer received " << last.size() << " samples:";
  for (double v : last) {
    std::cout << ' ' << v;
  }
  std::cout << '\n';

  // Nested GraphNode: two independent instances of the same blueprint.
  auto blueprint = std::make_shared<Graph>();
  blueprint->add_node("in", make_boundary_in(TypeId::FloatBuffer));
  blueprint->add_node("scale", std::make_unique<ScaleFilter>());
  blueprint->add_node("out", make_boundary_out(TypeId::FloatBuffer));
  if (auto* s = blueprint->find_node("scale")) {
    if (Pin* pin = s->find_input("gain")) {
      pin->literal = 3.0;
    }
  }
  blueprint->connect("in", "out", "scale", "in");
  blueprint->connect("scale", "out", "out", "in");

  Graph outer;
  outer.add_node("src", std::make_unique<SequenceProducer>());
  outer.add_node("a", std::make_unique<GraphNode>(blueprint, "inst_a"));
  outer.add_node("b", std::make_unique<GraphNode>(blueprint, "inst_b"));
  outer.add_node("sink_a", std::make_unique<BufferConsumer>());
  outer.add_node("sink_b", std::make_unique<BufferConsumer>());
  if (auto* p = outer.find_node("src")) {
    if (Pin* pin = p->find_input("count")) {
      pin->literal = 4.0;
    }
  }
  outer.connect("src", "out", "a", "in");
  outer.connect("src", "out", "b", "in");
  outer.connect("a", "out", "sink_a", "in");
  outer.connect("b", "out", "sink_b", "in");

  FlatGraph nested = engine.run_once(outer);
  auto* sa = dynamic_cast<BufferConsumer*>(nested.find_node("sink_a"));
  auto* sb = dynamic_cast<BufferConsumer*>(nested.find_node("sink_b"));
  if (!sa || !sb || sa->last().size() != 4 || sb->last().size() != 4) {
    std::cerr << "nested graph demo failed\n";
    return 1;
  }
  std::cout << "nested sinks ok (gain 3): first=" << sa->last().front()
            << " last=" << sa->last().back() << '\n';

  auto types = NodeFactory::instance().registered_types();
  std::cout << "registered node types: " << types.size() << '\n';
  std::cout << "demo ok\n";
  return 0;
}
