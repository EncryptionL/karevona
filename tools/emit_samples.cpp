// Usage: karevona-emit-samples <output-dir>
// Writes <type>.json files produced by the real serializers.
#include <filesystem>
#include <fstream>
#include <iostream>

#include "karevona/event.hpp"
#include "karevona/models.hpp"
#include "karevona/task.hpp"
#include "sim_providers.hpp"

namespace fs = std::filesystem;
using namespace karevona;

static void write(const fs::path& dir, const std::string& name, const nlohmann::json& j) {
    std::ofstream(dir / (name + ".json")) << j.dump(2) << '\n';
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: karevona-emit-samples <output-dir>\n";
        return 2;
    }
    const fs::path dir = argv[1];
    fs::create_directories(dir);

    sim::SimComputeProvider compute(ProviderId("sim-compute"), sim::default_sim_nodes(ProviderId("sim-compute")));
    auto nodes = compute.list_nodes().value();
    write(dir, "node", nodes.front());

    VmSpec spec;
    spec.name = "application-01";
    spec.vcpus = 8;
    spec.memory_bytes = uint64_t{16} << 30;
    auto vm = compute.create_vm(spec).value();
    write(dir, "vm", vm);
    write(dir, "resource", to_resource(vm));

    Event ev;
    ev.id = EventId("evt-0001");
    ev.type = events::kNodeFailed;
    ev.source = "node-x86-01";
    ev.timestamp = *parse_iso8601("2026-10-01T00:00:00.000Z");
    ev.payload = {{"reason", "heartbeat_timeout"}};
    ev.trace = {"trace-1", "req-1", ""};
    write(dir, "event", ev);

    TaskEngine engine;
    TaskSpec t;
    t.type = "vm.migrate";
    t.subject = vm.id.str();
    t.actor = "user-001";
    t.action = [](TaskContext& ctx) {
        ctx.report_progress(47, "copying");
        return Status::ok();
    };
    auto id = engine.submit(std::move(t)).value();
    auto snap = *engine.wait(id, std::chrono::seconds(5));
    write(dir, "task", snap);
    return 0;
}
