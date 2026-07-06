# Dvijoke

**движок** — the engine. A collection of classic graphics-API ports that run
2000s PC games in the browser as native WebAssembly builds.

Dvijoke is the engine technology behind [Igroteka](https://github.com/meerzulee/igroteka),
the 2000s-gaming-café-in-your-browser platform. It currently runs
**C&C Generals: Zero Hour** — menu, animated shell map, and skirmish — in a
browser tab at 30 fps.

The name reads two ways on purpose: Slavic readers see движок (Russian gamedev
slang for "game engine"); everyone else gets the joke.

## Architecture

Dvijoke is a family of API **frontends** over shared **backends**:

```
Frontends (one per legacy API)        Backends (one per modern API)
┌─────────────────────────────┐
│ d8web   Direct3D 8   (live) │      ┌──────────────────────────┐
│ d9web   Direct3D 9  (plan)  │ ──►  │ webgl2   (live)          │
│ ...                         │      │ webgpu   (planned)       │
└─────────────────────────────┘      │ native   (maybe: Dawn)   │
      state tracker + ShaderKey      └──────────────────────────┘
      + shader generators                 dumb translators
```

The rule: **backends stay dumb**. All intelligence — state tracking, shader
description (ShaderKey), program caching — lives in the frontend layer above
the `IBackend` seam. A backend only translates state snapshots into API calls.
Adding WebGPU means writing a WGSL emitter and a translator; nothing else
moves. Adding d9web means a new frontend that reuses the same backends.

Today the shared core and the WebGL2 backend live inside `d8web/` (it is the
only frontend). When the second frontend lands, `core/` and `backends/` get
extracted to the repo root — the seam is already designed for it.

## Contents

| Directory | Status | Purpose |
|---|---|---|
| `d8web/` | live | Direct3D 8 frontend + shared core + WebGL2 backend. Battle-tested by Zero Hour (200k LoC engine, fixed-function era) |
| `d9web/` | planned | Direct3D 9 frontend (GunZ/BF2-era games) |

Releases: `Dvijoke <version> (<backend>)` — e.g. `Dvijoke 0.4 (WebGL2)`,
later `Dvijoke 1.x (WebGPU)`. One engine version, N backend builds.

## The full stack for a game port

| Part | License | Where |
|---|---|---|
| Dvijoke (this repo) | MIT | frontends + backends |
| game engine fork | per-game (e.g. GPL v3) | e.g. a [GeneralsGameCode](https://github.com/TheSuperHackers/GeneralsGameCode) fork with the Emscripten toolchain, COM bridge and boot loader |

GPL engine forks and MIT Dvijoke never mix: the fork's bridge adapts the
game's D3D vtables onto Dvijoke's namespaced interfaces at the boundary.

## License

MIT. Game assets are copyrighted and never included — bring your own game
files. Not affiliated with or endorsed by EA.
