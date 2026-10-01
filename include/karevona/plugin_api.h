/*
 * KAREVONA plugin ABI, version 1.
 *
 * This is the only interface between the core and dynamically loaded
 * plugins. It is plain C so plugins can be built with any compiler/stdlib
 * and any language. Plugins may use C++ internally; no C++ types, exceptions
 * or STL objects may cross this boundary.
 *
 * Rules
 *  - All structs are append-only. `struct_size` lets the host detect newer
 *    or older layouts; fields are never reordered or removed within a major
 *    ABI version.
 *  - Strings are NUL-terminated UTF-8. Strings returned by the plugin
 *    (char** out parameters) are owned by the plugin and must be released
 *    with `free_string`. The host never frees plugin memory itself.
 *  - Payloads are JSON documents whose schema is the provider contract
 *    (see docs/architecture/plugins.md). This keeps the C surface tiny and
 *    evolvable.
 *  - A plugin must not call back into the host after `shutdown` returns.
 */
#ifndef KAREVONA_PLUGIN_API_H
#define KAREVONA_PLUGIN_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KAREVONA_PLUGIN_ABI_VERSION 1u
#define KAREVONA_PLUGIN_ENTRY_SYMBOL "karevona_plugin_entry_v1"

typedef enum karevona_status {
    KAREVONA_OK = 0,
    KAREVONA_ERR_INVALID_ARGUMENT = 1,
    KAREVONA_ERR_NOT_FOUND = 2,
    KAREVONA_ERR_ALREADY_EXISTS = 3,
    KAREVONA_ERR_FAILED_PRECONDITION = 4,
    KAREVONA_ERR_PERMISSION_DENIED = 5,
    KAREVONA_ERR_UNAVAILABLE = 6,
    KAREVONA_ERR_UNIMPLEMENTED = 7,
    KAREVONA_ERR_INTERNAL = 8
} karevona_status;

typedef enum karevona_log_level {
    KAREVONA_LOG_DEBUG = 0,
    KAREVONA_LOG_INFO = 1,
    KAREVONA_LOG_WARN = 2,
    KAREVONA_LOG_ERROR = 3
} karevona_log_level;

/* Bitmask of provider kinds a plugin implements. */
#define KAREVONA_PROVIDER_COMPUTE (1u << 0)
#define KAREVONA_PROVIDER_STORAGE (1u << 1)
#define KAREVONA_PROVIDER_NETWORK (1u << 2)
#define KAREVONA_PROVIDER_SECURITY (1u << 3)
#define KAREVONA_PROVIDER_AI (1u << 4)

/* Services the host offers to a plugin. Valid until `shutdown` returns. */
typedef struct karevona_host_v1 {
    uint32_t struct_size; /* sizeof(karevona_host_v1) */
    uint32_t abi_version; /* KAREVONA_PLUGIN_ABI_VERSION */
    void* host_context;
    void (*log)(void* host_context, karevona_log_level level, const char* component, const char* message);
} karevona_host_v1;

typedef struct karevona_plugin_v1 {
    uint32_t struct_size; /* sizeof(karevona_plugin_v1) */
    uint32_t abi_version; /* KAREVONA_PLUGIN_ABI_VERSION */
    const char* id;       /* stable reverse-DNS style id, e.g. "io.karevona.sim" */
    const char* name;
    const char* version;
    uint32_t provider_kinds;

    /* Create a plugin instance. config_json may be NULL or "{}". */
    karevona_status (*initialize)(const karevona_host_v1* host, const char* config_json, void** instance);

    /* {"providers":[{"id","name","kind","version","capabilities":[...],"architectures":[...]}]} */
    karevona_status (*describe)(void* instance, char** out_json);

    /* {"providers":{"<provider id>":{"state":"healthy|degraded|unavailable","message":""}}} */
    karevona_status (*health)(void* instance, char** out_json);

    /*
     * Execute "<kind>.<operation>" (e.g. "compute.list_vms") with a JSON
     * request. On failure the plugin should still set *out_json to
     * {"error":{"message":"..."}} when it can.
     */
    karevona_status (*invoke)(void* instance, const char* operation, const char* request_json, char** out_json);

    void (*free_string)(char* s);
    void (*shutdown)(void* instance);
} karevona_plugin_v1;

/* Exported by every plugin shared library. Returns a static descriptor. */
typedef const karevona_plugin_v1* (*karevona_plugin_entry_fn)(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* KAREVONA_PLUGIN_API_H */
