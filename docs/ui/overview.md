# Web console

Next.js (App Router) + TypeScript + Material UI, in `web/`. The UI is a **client of the Karevona API only**: it never
talks to hypervisors, storage systems or AI runtimes ([ADR 0001](../../adrs/0001-framework-core-boundary.md)).

## Structure

| Path | Purpose |
|---|---|
| `src/app/` | Routes: dashboard `/`, `/infrastructure`, `/storage`, `/security`, `/ai`, `/operations`, `/administration` |
| `src/modules/navigation.ts` | The information architecture (single list; plugins will add entries) |
| `src/sdk/` | Typed API client (`KarevonaClient`) and wire types mirroring `proto/karevona/v1` |
| `src/components/` | App shell, theme, shared page frame, cluster summary |
| `src/hooks/`, `src/lib/`, `src/plugins/` | Hooks, theme, reserved plugin UI extension point |

Only the dashboard shows live data (cluster info from `GET /api/v1/cluster`); the other areas are placeholders
until their milestones. `/api/v1/*` is proxied by Next to `KAREVONA_API_URL` (a REST gateway over the gRPC API —
planned; the controller currently serves gRPC only).

## Decisions and assumptions

* Wire types are hand-written for now; generating them from the `.proto` files (and a gRPC-Web/REST gateway) is the next UI/API step.
* Versions are pinned to a conservative, current stack (Next 15, React 19, MUI 7, TypeScript 5.9). Upgrading is routine maintenance, not an ADR.
* Streaming updates (WebSocket / server streams), charting, the universal VM page, storage command center, AI chat, event timeline and plugin UI (context §18) are future work.

Checks: `./scripts/dev web npm run typecheck | lint | test | build` (also run in CI).
