# Dvijoke

**движок** — the engine. Runs 2000s Direct3D 8 games in the browser as native
WebAssembly ports.

Dvijoke is the engine runtime behind [Igroteka](https://github.com/meerzulee/igroteka),
the 2000s-gaming-café-in-your-browser platform. It currently runs
**C&C Generals: Zero Hour** — main menu, animated shell map, and skirmish —
entirely in a browser tab at 30 fps.

The name reads two ways on purpose: Slavic readers see движок (Russian gamedev
slang for "game engine"); everyone else gets the joke.

## What's in this repo: d8web

This repository holds **d8web** (MIT), the heart of Dvijoke — a Direct3D 8 →
WebGL2 translation layer built for fixed-function-era games:

```
Game engine (D3D8 calls)
   └─ d8web frontend: IDirect3D8/Device8 interfaces + state tracker
        └─ ShaderKey: FFP state bits → generated GLSL uber-shaders
             └─ IBackend seam
                  └─ WebGL2 backend (today) │ WebGPU backend (planned)
```

Backends stay dumb: all intelligence (state tracking, shader description,
program caching) lives above the `IBackend` seam. A backend only translates
state snapshots into API calls. The WebGPU backend is a WGSL emitter + a new
translator — nothing else moves.

Battle-tested D3D8 surface (driven by a real 200k-LoC engine, not demos):
fixed-function pipeline with per-stage combine ops (incl. MULTIPLYADD/DOTPRODUCT3),
texture-coordinate transforms + camera-space texgen, directional/point lights,
fog, alpha test/blend, DXT1–5 (incl. DXT2/4 aliasing), luminance formats,
partial-rect locks, `CopyRects`, texture-owned level surfaces, mip-chain
integrity clamps.

## The full Dvijoke stack

| Part | License | Where |
|---|---|---|
| `d8web` (this repo) | MIT | translation layer + backends |
| engine fork (`zh-web`) | GPL v3 | fork of [GeneralsGameCode](https://github.com/TheSuperHackers/GeneralsGameCode) with the Emscripten/WASM toolchain, COM bridge and boot loader |

The GPL engine fork and MIT d8web never mix: the fork's `wasm/d8web_bridge/`
adapts DXVK-style D3D8 vtables onto d8web's namespaced interfaces at the
boundary.

Releases are named `Dvijoke <version> (<backend>)` — e.g. `Dvijoke 0.4 (WebGL2)`,
later `Dvijoke 1.x (WebGPU)`. One engine version, N backend builds.

## Status

- Zero Hour boots, renders its animated main menu with correct terrain,
  text, textures and UI, and loads into skirmish
- See `INTERFACE.md` for the scoped D3D8 contract (derived from grepping the
  engine's actual call sites)
- Examples in `examples/`: lit cube, textured quad, UI quads

## License

MIT. Game assets are copyrighted and never included — bring your own game
files. Not affiliated with or endorsed by EA.
