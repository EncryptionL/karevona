#!/usr/bin/env python3
"""Validate JSON Schemas and check that examples + serializer output conform.

  validate_schemas.py --schemas schemas --examples schemas/examples [--emitter build/tools/karevona-emit-samples]

1. Every schema in <schemas>/karevona/v1 is a valid JSON Schema (2020-12).
2. Every <examples>/<name>.json (or <name>.<variant>.json) validates against
   <name>.schema.json (valid) -- files named <name>.invalid.*.json must FAIL.
3. With --emitter, the C++ serializers' real output for each type is validated
   too, so schemas cannot drift from the code.
"""
import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

from jsonschema import Draft202012Validator, FormatChecker
from referencing import Registry, Resource
from referencing.jsonschema import DRAFT202012


def load(path: Path):
    with path.open() as f:
        return json.load(f)


def make_validator(schema, registry, store):
    """jsonschema >= 4.18 resolves refs via `referencing`; older releases (Ubuntu 24.04's 4.10) need a RefResolver."""
    try:
        return Draft202012Validator(schema, registry=registry, format_checker=FormatChecker())
    except TypeError:
        from jsonschema import RefResolver  # deprecated in new releases, but only reached on old ones

        resolver = RefResolver(schema.get("$id", ""), schema, store=store)
        return Draft202012Validator(schema, resolver=resolver, format_checker=FormatChecker())


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--schemas", required=True, type=Path)
    ap.add_argument("--examples", required=True, type=Path)
    ap.add_argument("--emitter", type=Path)
    args = ap.parse_args()

    schema_dir = args.schemas / "karevona" / "v1"
    validators = {}
    failures = 0

    # Schemas reference each other by $id-relative URIs; resolve them locally.
    registry = Registry()
    store = {}
    for path in sorted(schema_dir.glob("*.schema.json")):
        schema = load(path)
        store[schema["$id"]] = schema
        registry = registry.with_resource(schema["$id"], Resource.from_contents(schema, DRAFT202012))

    for path in sorted(schema_dir.glob("*.schema.json")):
        schema = load(path)
        try:
            Draft202012Validator.check_schema(schema)
        except Exception as exc:  # noqa: BLE001
            print(f"FAIL  schema {path.name}: {exc}")
            failures += 1
            continue
        validators[path.name.removesuffix(".schema.json")] = make_validator(schema, registry, store)
        print(f"ok    schema {path.name}")

    def check(path: Path, name: str, expect_valid: bool):
        nonlocal failures
        validator = validators.get(name)
        if validator is None:
            print(f"FAIL  {path.name}: no schema named '{name}'")
            failures += 1
            return
        errors = sorted(validator.iter_errors(load(path)), key=lambda e: list(e.path))
        if bool(errors) == expect_valid:
            detail = errors[0].message if errors else "expected a validation error, got none"
            print(f"FAIL  {path.name}: {detail}")
            failures += 1
        else:
            print(f"ok    {path.name} ({'valid' if expect_valid else 'rejected as expected'})")

    for path in sorted(args.examples.glob("*.json")):
        parts = path.name.removesuffix(".json").split(".")
        check(path, parts[0], expect_valid="invalid" not in parts[1:])

    if args.emitter:
        with tempfile.TemporaryDirectory() as tmp:
            subprocess.run([str(args.emitter), tmp], check=True)
            for path in sorted(Path(tmp).glob("*.json")):
                check(path, path.stem, expect_valid=True)

    print("schemas: FAILED" if failures else "schemas: all checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
