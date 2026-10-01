// Node agent skeleton. Runs close to the host kernel/hardware (not
// necessarily in a container). For now it only reports what it is: its
// architecture is the first input to architecture-aware scheduling.
// Registration with a controller arrives with the cluster-membership milestone.
#include <iostream>

#include "karevona/architecture.hpp"
#include "karevona/models.hpp"
#include "karevona/version.hpp"

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--version") {
        std::cout << "karevona-node-agent " << karevona::kVersion << '\n';
        return 0;
    }
    karevona::NodeInfo node;
    node.id = karevona::NodeId("local");
    node.name = "local";
    node.architecture = karevona::host_architecture();
    node.state = karevona::NodeState::Discovered;
    std::cout << nlohmann::json(node).dump(2) << '\n';
    return 0;
}
