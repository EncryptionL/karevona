# Karevona web console

Next.js (App Router) + TypeScript + Material UI. It consumes the Karevona API
only (`src/sdk`); it never talks to hypervisors, storage systems, or AI runtimes.

All commands run inside the dev container (no host Node.js needed):

```bash
./scripts/dev web npm ci
./scripts/dev web npm run dev        # http://localhost:3000
./scripts/dev web npm run typecheck && ./scripts/dev web npm run lint && ./scripts/dev web npm test
./scripts/dev web npm run build
```

`/api/v1/*` is proxied to `KAREVONA_API_URL` (default `http://localhost:8080`).

Layout: `src/app` routes · `src/components` · `src/modules` navigation/IA ·
`src/sdk` typed API client · `src/hooks` · `src/lib` · `src/plugins` (reserved).
See `docs/ui/overview.md`.
