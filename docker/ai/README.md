# AI runtime images

Reserved for self-hosted AI runtime images (e.g. Ollama / vLLM / llama.cpp
wrappers) used by AI provider plugins. The development compose file exposes an
optional `ollama` service behind the `ai` profile; no Karevona code depends on
any specific runtime (see docs/ai/overview.md).
