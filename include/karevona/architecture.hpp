// CPU architecture is a first-class node/VM property and scheduling input.
#pragma once

#include <optional>
#include <string>

namespace karevona {

enum class Architecture { Unknown, X86, X86_64, Arm, Aarch64 };

const char* to_string(Architecture arch);
std::optional<Architecture> parse_architecture(const std::string& text);

// Architecture of the machine this binary was compiled for.
Architecture host_architecture();

enum class Compatibility {
    Native,        // guest arch == host arch
    Emulated,      // different arch, only via explicit emulation (e.g. QEMU TCG)
    Incompatible,  // cannot run
};

// Whether a guest of `guest` architecture can run on a host of `host`.
// Emulation is never assumed: it must be explicitly allowed by the caller
// (policy), and it is always reported as distinct from Native.
Compatibility guest_compatibility(Architecture guest, Architecture host, bool allow_emulation);

// Live migration is only supported between identical architectures.
// Cross-architecture recovery is a separate, explicit workflow.
bool can_live_migrate(Architecture source, Architecture destination);

}  // namespace karevona
