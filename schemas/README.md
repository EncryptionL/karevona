# schemas

JSON Schema (2020-12) for the JSON forms of core objects: resource, node, VM, event, task (`karevona/v1`).
`examples/` holds valid examples and intentionally invalid ones (`<name>.invalid.<why>.json`). CI also validates what
the real C++ serializers produce (`karevona-emit-samples`), so schema and code cannot drift.
Run: `python3 tools/validate_schemas.py --schemas schemas --examples schemas/examples`.
