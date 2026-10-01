# Karevona web console development image (Node.js LTS; no host Node needed).
FROM node:22-bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends git ca-certificates \
    && rm -rf /var/lib/apt/lists/*

ENV NEXT_TELEMETRY_DISABLED=1
WORKDIR /workspace/web
USER node
CMD ["npm", "run", "dev"]
