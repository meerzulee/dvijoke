# d8web — Implementation Contract

The D3D8 API surface the SAGE engine actually exercises, extracted by grepping
`DX8CALL`/`DX8CALL_HRES`/direct device calls across
`Core/Libraries/Source/WWVegas/WW3D2/` in GeneralsGameCode (July 2026).
This is the contract; anything outside it is stubbed until a caller appears.

Legend: **P0** cube demo · **P1** required for ZH rendering · **P2** required for
correctness/fullscreen edge cases · **stub** log-and-succeed until needed.

## IDirect3D8

| Method | Pri |
|---|---|
| CreateDevice | P0 |
| GetAdapterDisplayMode / GetAdapterCount / GetAdapterIdentifier | P1 |
| CheckDeviceFormat / CheckDepthStencilMatch | P1 (caps queries during init) |
| GetDeviceCaps | P1 |
| EnumAdapterModes / GetAdapterModeCount | P2 |

## IDirect3DDevice8 — frame & swap

| Method | Pri | Notes |
|---|---|---|
| BeginScene / EndScene | P0 | no-op markers |
| Clear | P0 | color+depth+stencil |
| Present | P0 | commit frame |
| Reset | P2 | resize handling |
| TestCooperativeLevel | P2 | always D3D_OK in browser |
| GetDisplayMode | P1 |
| GetBackBuffer / GetFrontBuffer | P2 | GetFrontBuffer = screenshots, needs readback |
| CreateAdditionalSwapChain | stub | engine references it; browser has one canvas |
| SetGammaRamp | stub | apply via fullscreen post pass later, or ignore |
| GetAvailableTextureMem | P1 | report a large constant |
| ValidateDevice | P1 | always pass |
| ResourceManagerDiscardBytes | stub |

## IDirect3DDevice8 — state

| Method | Pri | Notes |
|---|---|---|
| SetRenderState | P0 | see render-state table below |
| SetTextureStageState | P1 | stages 0–1 first (terrain multitexture), grow as needed |
| SetTexture | P0 | stages 0–7 accepted, 0–1 honored initially |
| SetTransform / GetTransform | P0 | WORLD, VIEW, PROJECTION (+ TEXTURE0/1 later) |
| SetViewport | P0 |
| SetMaterial | P0 |
| SetLight / LightEnable | P0 | directional first (ZH sun), then point |
| SetClipPlane | P2 | water reflection path |
| SetVertexShader | P0 | **FVF codes only** — report VertexShaderVersion=0 so engine stays on FFP path |
| SetPixelShader / SetPixelShaderConstant / SetVertexShaderConstant | stub | engine guards these behind caps we won't advertise |

## IDirect3DDevice8 — geometry & draw

| Method | Pri |
|---|---|
| CreateVertexBuffer / CreateIndexBuffer | P0 |
| SetStreamSource (stream 0 only) | P0 |
| SetIndices | P0 |
| DrawIndexedPrimitive | P0 |
| DrawPrimitiveUP / DrawIndexedPrimitiveUP | P1 (UI, particles) |

## IDirect3DDevice8 — resources & render targets

| Method | Pri |
|---|---|
| CreateTexture | P0 |
| UpdateTexture | P1 (texture upload path uses sysmem→default copies) |
| CopyRects | P1 |
| CreateImageSurface | P1 |
| CreateDepthStencilSurface / GetDepthStencilSurface | P2 |
| SetRenderTarget / GetRenderTarget | P2 (water/shadow RT passes) |

## Resource interfaces

| Interface | Methods | Pri |
|---|---|---|
| IDirect3DVertexBuffer8 | Lock, Unlock, Release | P0 |
| IDirect3DIndexBuffer8 | Lock, Unlock, Release | P0 |
| IDirect3DTexture8 | GetSurfaceLevel, GetLevelDesc, LockRect, UnlockRect, AddRef, Release | P0/P1 |
| IDirect3DSurface8 | LockRect, UnlockRect, GetDesc, Release | P1 |

## Render states the engine sets (initial honor list)

Culling: `D3DRS_CULLMODE` · Depth: `ZENABLE, ZWRITEENABLE, ZFUNC, ZBIAS` ·
Blend: `ALPHABLENDENABLE, SRCBLEND, DESTBLEND` · Alpha test: `ALPHATESTENABLE,
ALPHAREF, ALPHAFUNC` · Lighting: `LIGHTING, AMBIENT, SPECULARENABLE, COLORVERTEX` +
material source states · Fog: `FOGENABLE, FOGCOLOR, FOGTABLEMODE (LINEAR),
FOGSTART, FOGEND` · Misc: `DITHERENABLE, FILLMODE` (accept, mostly ignore)

Everything else: accept, store, log-once at debug level. Unknown-state crashes are
forbidden — the wrapper must never be the reason the engine dies.

## Formats (initial)

Textures: `D3DFMT_A8R8G8B8, X8R8G8B8, R5G6B5, A1R5G5B5, A4R4G4B4, DXT1–5`
(DXT via `WEBGL_compressed_texture_s3tc`, fallback CPU-decompress) ·
Index: `D3DFMT_INDEX16` (INDEX32 rare) · Depth: `D16/D24S8 → DEPTH24_STENCIL8`

## D3D8 quirks encoded here

- `SetVertexShader(DWORD)` doubles as SetFVF — a handle <0xFFFF... is an FVF code.
  We only support FVF codes.
- No `SetFVF`, no `CreateVertexDeclaration` — those are D3D9isms.
- `DrawIndexedPrimitive` has no BaseVertexIndex param (it lives on SetIndices) —
  D3D9 moved it; watch when porting d3d9-webgl code.
- Lock flags matter: `D3DLOCK_DISCARD`/`NOOVERWRITE` on dynamic VBs drive the
  ring-buffer path in backends.
