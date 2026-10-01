#include "karevona/architecture.hpp"

namespace karevona {

const char* to_string(Architecture arch) {
    switch (arch) {
        case Architecture::X86:
            return "x86";
        case Architecture::X86_64:
            return "x86_64";
        case Architecture::Arm:
            return "arm";
        case Architecture::Aarch64:
            return "aarch64";
        case Architecture::Unknown:
            break;
    }
    return "unknown";
}

std::optional<Architecture> parse_architecture(const std::string& t) {
    if (t == "x86" || t == "i386" || t == "i686") return Architecture::X86;
    if (t == "x86_64" || t == "amd64") return Architecture::X86_64;
    if (t == "arm" || t == "armv7" || t == "armv7l") return Architecture::Arm;
    if (t == "aarch64" || t == "arm64") return Architecture::Aarch64;
    if (t == "unknown") return Architecture::Unknown;
    return std::nullopt;
}

Architecture host_architecture() {
#if defined(__x86_64__) || defined(_M_X64)
    return Architecture::X86_64;
#elif defined(__aarch64__) || defined(_M_ARM64)
    return Architecture::Aarch64;
#elif defined(__i386__) || defined(_M_IX86)
    return Architecture::X86;
#elif defined(__arm__) || defined(_M_ARM)
    return Architecture::Arm;
#else
    return Architecture::Unknown;
#endif
}

Compatibility guest_compatibility(Architecture guest, Architecture host, bool allow_emulation) {
    if (guest == Architecture::Unknown || host == Architecture::Unknown) return Compatibility::Incompatible;
    if (guest == host) return Compatibility::Native;
    return allow_emulation ? Compatibility::Emulated : Compatibility::Incompatible;
}

bool can_live_migrate(Architecture source, Architecture destination) {
    return source != Architecture::Unknown && source == destination;
}

}  // namespace karevona
