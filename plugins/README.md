# plugins

Provider plugins, built against the C ABI in `include/karevona/plugin_api.h`
([docs](../docs/architecture/plugins.md)).

* `sim/` — simulated compute/storage/network/security/AI providers: used by tests and `--simulate`, and built as the
  loadable module `karevona_plugin_sim` (the reference plugin).

Real providers (Proxmox, vSphere, QEMU/KVM, Ceph, OVS, ClamAV, AI runtimes, …) are future work; each gets its own
directory and must pass the provider contract tests.
