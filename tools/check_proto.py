#!/usr/bin/env python3
"""Convention checks for proto/karevona/v1 (see docs/architecture/api.md).

 1. package is karevona.v1
 2. every enum's zero value is <ENUM>_UNSPECIFIED (screaming-snake of the enum name)
 3. mutating RPCs (Create/Delete/Start/Stop/Migrate/Attach/Detach/Cancel/Register)
    take a request message with `RequestMeta meta` (idempotency + correlation)
 4. mutating RPCs that start long-running work return a message carrying a TaskRef
    (CancelTask returns the Task itself)
 5. no vendor names in contract identifiers (provider neutrality)
"""
import re
import sys
from pathlib import Path

MUTATING = re.compile(r"^(Create|Delete|Start|Stop|Migrate|Attach|Detach|Cancel|Register)")
VENDORS = ("proxmox", "vmware", "vsphere", "qemu", "kvm", "ceph", "kafka", "valkey", "ollama", "openai")


def snake_upper(name: str) -> str:
    return re.sub(r"(?<!^)(?=[A-Z])", "_", name).upper()


def message_bodies(text: str) -> dict:
    out = {}
    for m in re.finditer(r"message\s+(\w+)\s*\{(.*?)\n\}", text, re.S):
        out[m.group(1)] = m.group(2)
    return out


def main(files) -> int:
    errors = []
    all_messages = {}
    texts = {}
    for f in files:
        text = Path(f).read_text()
        texts[f] = text
        all_messages.update(message_bodies(text))

    for f, text in texts.items():
        if not re.search(r"^package\s+karevona\.v1;", text, re.M):
            errors.append(f"{f}: package must be karevona.v1")

        for m in re.finditer(r"enum\s+(\w+)\s*\{(.*?)\}", text, re.S):
            name, body = m.group(1), m.group(2)
            first = re.search(r"(\w+)\s*=\s*0\s*;", body)
            expected = snake_upper(name) + "_UNSPECIFIED"
            if not first or first.group(1) != expected:
                errors.append(f"{f}: enum {name} must have {expected} = 0")

        for m in re.finditer(r"rpc\s+(\w+)\s*\(\s*(\w+)\s*\)\s*returns\s*\(\s*(?:stream\s+)?(\w+)\s*\)", text):
            rpc, req, resp = m.groups()
            if MUTATING.match(rpc):
                if "RequestMeta meta" not in all_messages.get(req, ""):
                    errors.append(f"{f}: {rpc}: request {req} must carry `RequestMeta meta`")
                body = all_messages.get(resp, "")
                if rpc != "CancelTask" and "TaskRef" not in body:
                    errors.append(f"{f}: {rpc}: response {resp} must carry a TaskRef (long-running ops return tasks)")

        for ident in re.findall(r"\b(?:message|enum|service|rpc)\s+(\w+)", text):
            if any(v in ident.lower() for v in VENDORS):
                errors.append(f"{f}: vendor-specific identifier in public contract: {ident}")

    for e in errors:
        print(f"FAIL  {e}")
    print(f"proto conventions: {'FAILED' if errors else 'ok'} ({len(files)} files)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
