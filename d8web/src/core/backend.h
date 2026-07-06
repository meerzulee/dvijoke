// d8web core — IBackend: the multi-backend seam.
// Backends are dumb translators: state snapshots + draw commands in, API calls out.
// No D3D8 interface types, no frontend logic, no shader-policy decisions here.
// Implementations: backends/webgl2 (v1), backends/webgpu (planned v2),
// optional native backend (Dawn/sokol) later.
#pragma once

#include "shaderkey.h"
#include "state.h"

#include <cstdint>

namespace d8web {

enum class BufferKind : uint8_t { Vertex, Index16, Index32 };

enum class TexFormat : uint8_t {
    RGBA8,     // from A8R8G8B8 / X8R8G8B8 (swizzled by frontend)
    RGB565,
    RGBA4,
    RGB5A1,
    L8,
    A8L8,
    DXT1, DXT3, DXT5,
};

struct DrawGeometry {
    // Indexed path: vb + ib set. Non-indexed: ib = 0.
    BackendHandle vb = 0;
    UINT vbStride = 0;
    BackendHandle ib = 0;
    bool index32 = false;
    UINT baseVertexIndex = 0;   // D3D8: lives on SetIndices, applied to index values
    D3DPRIMITIVETYPE primitive = D3DPT_TRIANGLELIST;
    UINT startIndex = 0;        // indexed path
    UINT startVertex = 0;       // non-indexed path
    UINT primCount = 0;
    VertexLayout layout;
};

class IBackend {
public:
    virtual ~IBackend() = default;

    virtual bool init(int width, int height) = 0;
    virtual void resize(int width, int height) = 0;

    // Buffers
    virtual BackendHandle createBuffer(BufferKind kind, UINT byteSize, bool dynamic) = 0;
    virtual void updateBuffer(BackendHandle h, UINT offset, const void* data, UINT size,
                              bool discard) = 0;
    virtual void destroyBuffer(BackendHandle h) = 0;

    // Textures (2D only; cube/volume when a caller appears)
    virtual BackendHandle createTexture(UINT width, UINT height, UINT levels, TexFormat format) = 0;
    virtual void updateTexture(BackendHandle h, UINT level, UINT width, UINT height,
                               TexFormat format, const void* data, UINT byteSize) = 0;
    virtual void destroyTexture(BackendHandle h) = 0;

    // Frame
    virtual void setViewport(const D3DVIEWPORT8& vp) = 0;
    virtual void clear(bool color, bool depth, bool stencil,
                       D3DCOLOR argb, float z, DWORD stencilValue) = 0;
    virtual void draw(const DrawGeometry& geo, const StateSnapshot& state, const ShaderKey& key) = 0;
    virtual void present() = 0;

    // Diagnostics
    virtual const char* name() const = 0;
};

// Factory implemented per backend translation unit
IBackend* createWebGL2Backend();
// IBackend* createWebGPUBackend();   // v2

}  // namespace d8web
