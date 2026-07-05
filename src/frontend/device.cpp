// d8web frontend — IDirect3D8 / IDirect3DDevice8 / resources
// Records D3D8 calls into a StateSnapshot and forwards draws to the active IBackend.
// This file must stay backend-agnostic: no GL/WebGPU includes.

#include "d8web/d3d8.h"

#include "../core/backend.h"
#include "../core/shaderkey.h"
#include "../core/state.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace d8web {
namespace {

// ---------- format helpers ----------

struct FormatInfo {
    TexFormat backend;
    UINT bytesPerPixel;   // 0 = block-compressed
    bool needsSwizzle;    // ARGB-family CPU reorder before upload
};

bool formatInfo(D3DFORMAT f, FormatInfo& out) {
    switch (f) {
        case D3DFMT_A8R8G8B8: out = {TexFormat::RGBA8, 4, true}; return true;
        case D3DFMT_X8R8G8B8: out = {TexFormat::RGBA8, 4, true}; return true;
        case D3DFMT_R5G6B5: out = {TexFormat::RGB565, 2, false}; return true;
        case D3DFMT_A1R5G5B5: out = {TexFormat::RGB5A1, 2, true}; return true;
        case D3DFMT_A4R4G4B4: out = {TexFormat::RGBA4, 2, true}; return true;
        case D3DFMT_L8: out = {TexFormat::L8, 1, false}; return true;
        case D3DFMT_A8L8: out = {TexFormat::A8L8, 2, false}; return true;
        case D3DFMT_DXT1: out = {TexFormat::DXT1, 0, false}; return true;
        case D3DFMT_DXT3: out = {TexFormat::DXT3, 0, false}; return true;
        case D3DFMT_DXT5: out = {TexFormat::DXT5, 0, false}; return true;
        default: return false;
    }
}

UINT surfaceBytes(D3DFORMAT f, UINT w, UINT h) {
    FormatInfo fi;
    if (!formatInfo(f, fi)) return w * h * 4;
    if (fi.bytesPerPixel) return w * h * fi.bytesPerPixel;
    UINT blocks = ((w + 3) / 4) * ((h + 3) / 4);
    return blocks * ((f == D3DFMT_DXT1) ? 8 : 16);
}

// CPU reorder for upload: D3D component order → GL component order
void swizzleForUpload(D3DFORMAT f, BYTE* data, UINT w, UINT h) {
    const UINT n = w * h;
    switch (f) {
        case D3DFMT_A8R8G8B8:
        case D3DFMT_X8R8G8B8: {
            // BGRA bytes → RGBA bytes; X8: force alpha opaque
            const bool forceAlpha = (f == D3DFMT_X8R8G8B8);
            for (UINT i = 0; i < n; ++i) {
                BYTE* p = data + i * 4;
                std::swap(p[0], p[2]);
                if (forceAlpha) p[3] = 0xFF;
            }
            break;
        }
        case D3DFMT_A4R4G4B4: {
            // ARGB4444 → RGBA4444 (GL_UNSIGNED_SHORT_4_4_4_4 packs R in top nibble)
            auto* px = reinterpret_cast<uint16_t*>(data);
            for (UINT i = 0; i < n; ++i) {
                uint16_t v = px[i];
                uint16_t a = (v >> 12) & 0xF, r = (v >> 8) & 0xF, g = (v >> 4) & 0xF, b = v & 0xF;
                px[i] = uint16_t((r << 12) | (g << 8) | (b << 4) | a);
            }
            break;
        }
        case D3DFMT_A1R5G5B5: {
            // ARGB1555 → RGBA5551
            auto* px = reinterpret_cast<uint16_t*>(data);
            for (UINT i = 0; i < n; ++i) {
                uint16_t v = px[i];
                uint16_t a = (v >> 15) & 0x1, r = (v >> 10) & 0x1F, g = (v >> 5) & 0x1F, b = v & 0x1F;
                px[i] = uint16_t((r << 11) | (g << 6) | (b << 1) | a);
            }
            break;
        }
        default: break;
    }
}

// ---------- resources ----------

class RefCounted8 {
public:
    ULONG addRef() { return ++m_ref; }
    ULONG release() {
        ULONG r = --m_ref;
        if (r == 0) delete this;
        return r;
    }
    virtual ~RefCounted8() = default;

private:
    ULONG m_ref = 1;
};

class VertexBuffer8 final : public IDirect3DVertexBuffer8, public RefCounted8 {
public:
    VertexBuffer8(IBackend* backend, UINT length, DWORD usage, DWORD fvf)
        : m_backend(backend), m_shadow(length), m_fvf(fvf),
          m_dynamic((usage & D3DUSAGE_DYNAMIC) != 0) {
        m_handle = backend->createBuffer(BufferKind::Vertex, length, m_dynamic);
    }
    ~VertexBuffer8() override { m_backend->destroyBuffer(m_handle); }

    ULONG AddRef() override { return addRef(); }
    ULONG Release() override { return release(); }

    HRESULT Lock(UINT offset, UINT size, BYTE** data, DWORD flags) override {
        if (!data) return D3DERR_INVALIDCALL;
        m_lockOffset = offset;
        m_lockSize = size ? size : UINT(m_shadow.size()) - offset;
        m_lockFlags = flags;
        *data = m_shadow.data() + offset;
        return D3D_OK;
    }
    HRESULT Unlock() override {
        if ((m_lockFlags & D3DLOCK_READONLY) == 0)
            m_backend->updateBuffer(m_handle, m_lockOffset, m_shadow.data() + m_lockOffset,
                                    m_lockSize, (m_lockFlags & D3DLOCK_DISCARD) != 0);
        return D3D_OK;
    }

    BackendHandle handle() const { return m_handle; }
    DWORD fvf() const { return m_fvf; }

private:
    IBackend* m_backend;
    std::vector<BYTE> m_shadow;
    BackendHandle m_handle = 0;
    DWORD m_fvf = 0;
    bool m_dynamic = false;
    UINT m_lockOffset = 0, m_lockSize = 0;
    DWORD m_lockFlags = 0;
};

class IndexBuffer8 final : public IDirect3DIndexBuffer8, public RefCounted8 {
public:
    IndexBuffer8(IBackend* backend, UINT length, DWORD usage, D3DFORMAT format)
        : m_backend(backend), m_shadow(length), m_index32(format == D3DFMT_INDEX32),
          m_dynamic((usage & D3DUSAGE_DYNAMIC) != 0) {
        m_handle = backend->createBuffer(m_index32 ? BufferKind::Index32 : BufferKind::Index16,
                                         length, m_dynamic);
    }
    ~IndexBuffer8() override { m_backend->destroyBuffer(m_handle); }

    ULONG AddRef() override { return addRef(); }
    ULONG Release() override { return release(); }

    HRESULT Lock(UINT offset, UINT size, BYTE** data, DWORD flags) override {
        if (!data) return D3DERR_INVALIDCALL;
        m_lockOffset = offset;
        m_lockSize = size ? size : UINT(m_shadow.size()) - offset;
        m_lockFlags = flags;
        *data = m_shadow.data() + offset;
        return D3D_OK;
    }
    HRESULT Unlock() override {
        if ((m_lockFlags & D3DLOCK_READONLY) == 0)
            m_backend->updateBuffer(m_handle, m_lockOffset, m_shadow.data() + m_lockOffset,
                                    m_lockSize, (m_lockFlags & D3DLOCK_DISCARD) != 0);
        return D3D_OK;
    }

    BackendHandle handle() const { return m_handle; }
    bool index32() const { return m_index32; }

private:
    IBackend* m_backend;
    std::vector<BYTE> m_shadow;
    BackendHandle m_handle = 0;
    bool m_index32 = false;
    bool m_dynamic = false;
    UINT m_lockOffset = 0, m_lockSize = 0;
    DWORD m_lockFlags = 0;
};

class Texture8;

// Surface that views one mip level of a texture, or owns standalone sysmem storage
class Surface8 final : public IDirect3DSurface8, public RefCounted8 {
public:
    // Standalone (CreateImageSurface)
    Surface8(UINT w, UINT h, D3DFORMAT f)
        : m_width(w), m_height(h), m_format(f), m_shadow(surfaceBytes(f, w, h)) {}
    // Level view
    Surface8(Texture8* owner, UINT level, UINT w, UINT h, D3DFORMAT f)
        : m_owner(owner), m_level(level), m_width(w), m_height(h), m_format(f) {}

    ULONG AddRef() override { return addRef(); }
    ULONG Release() override { return release(); }

    HRESULT GetDesc(D3DSURFACE_DESC* desc) override {
        if (!desc) return D3DERR_INVALIDCALL;
        *desc = {};
        desc->Format = m_format;
        desc->Type = D3DRTYPE_SURFACE;
        desc->Pool = D3DPOOL_SYSTEMMEM;
        desc->Width = m_width;
        desc->Height = m_height;
        desc->Size = surfaceBytes(m_format, m_width, m_height);
        return D3D_OK;
    }
    HRESULT LockRect(D3DLOCKED_RECT* locked, const RECT* rect, DWORD flags) override;
    HRESULT UnlockRect() override;

    UINT width() const { return m_width; }
    UINT height() const { return m_height; }
    D3DFORMAT format() const { return m_format; }
    BYTE* pixels() { return m_shadow.data(); }
    Texture8* owner() const { return m_owner; }
    UINT level() const { return m_level; }

private:
    Texture8* m_owner = nullptr;
    UINT m_level = 0;
    UINT m_width, m_height;
    D3DFORMAT m_format;
    std::vector<BYTE> m_shadow;
};

class Texture8 final : public IDirect3DTexture8, public RefCounted8 {
public:
    Texture8(IBackend* backend, UINT w, UINT h, UINT levels, D3DFORMAT format, D3DPOOL pool)
        : m_backend(backend), m_width(w), m_height(h), m_format(format), m_pool(pool) {
        if (levels == 0) {
            levels = 1;
            UINT s = std::max(w, h);
            while (s > 1) { s >>= 1; levels++; }
        }
        m_levels = levels;
        m_shadow.resize(levels);
        UINT lw = w, lh = h;
        for (UINT i = 0; i < levels; ++i) {
            m_shadow[i].resize(surfaceBytes(format, lw, lh));
            lw = std::max(1u, lw >> 1);
            lh = std::max(1u, lh >> 1);
        }
        FormatInfo fi;
        if (formatInfo(format, fi)) {
            m_backendFormat = fi.backend;
            m_hasAlpha = format != D3DFMT_X8R8G8B8 && format != D3DFMT_R5G6B5 &&
                         format != D3DFMT_L8;
            m_handle = backend->createTexture(w, h, levels, fi.backend);
        } else {
            std::fprintf(stderr, "[d8web] unsupported texture format 0x%X\n", format);
        }
    }
    ~Texture8() override {
        if (m_handle) m_backend->destroyTexture(m_handle);
    }

    ULONG AddRef() override { return addRef(); }
    ULONG Release() override { return release(); }
    D3DRESOURCETYPE GetType() override { return D3DRTYPE_TEXTURE; }
    DWORD SetPriority(DWORD) override { return 0; }
    DWORD GetPriority() override { return 0; }
    DWORD GetLevelCount() override { return m_levels; }

    HRESULT GetLevelDesc(UINT level, D3DSURFACE_DESC* desc) override {
        if (!desc || level >= m_levels) return D3DERR_INVALIDCALL;
        *desc = {};
        desc->Format = m_format;
        desc->Type = D3DRTYPE_TEXTURE;
        desc->Pool = m_pool;
        desc->Width = std::max(1u, m_width >> level);
        desc->Height = std::max(1u, m_height >> level);
        desc->Size = surfaceBytes(m_format, desc->Width, desc->Height);
        return D3D_OK;
    }

    HRESULT GetSurfaceLevel(UINT level, IDirect3DSurface8** surface) override {
        if (!surface || level >= m_levels) return D3DERR_INVALIDCALL;
        *surface = new Surface8(this, level, std::max(1u, m_width >> level),
                                std::max(1u, m_height >> level), m_format);
        return D3D_OK;
    }

    HRESULT LockRect(UINT level, D3DLOCKED_RECT* locked, const RECT* rect, DWORD flags) override {
        if (!locked || level >= m_levels) return D3DERR_INVALIDCALL;
        (void)rect;  // whole-surface locks only; partial-rect support when a caller appears
        (void)flags;
        FormatInfo fi{};
        formatInfo(m_format, fi);
        UINT w = std::max(1u, m_width >> level);
        locked->pBits = m_shadow[level].data();
        locked->Pitch = fi.bytesPerPixel ? INT(w * fi.bytesPerPixel)
                                         : INT(((w + 3) / 4) * (m_format == D3DFMT_DXT1 ? 8 : 16));
        m_lockedLevel = level;
        return D3D_OK;
    }

    HRESULT UnlockRect(UINT level) override {
        if (level >= m_levels || !m_handle) return D3DERR_INVALIDCALL;
        uploadLevel(level);
        return D3D_OK;
    }

    void uploadLevel(UINT level) {
        FormatInfo fi{};
        if (!formatInfo(m_format, fi)) return;
        UINT w = std::max(1u, m_width >> level), h = std::max(1u, m_height >> level);
        std::vector<BYTE>& src = m_shadow[level];
        if (fi.needsSwizzle) {
            // Convert a scratch copy; shadow keeps D3D layout so re-locks see what the
            // engine wrote.
            m_scratch.assign(src.begin(), src.end());
            swizzleForUpload(m_format, m_scratch.data(), w, h);
            m_backend->updateTexture(m_handle, level, w, h, m_backendFormat,
                                     m_scratch.data(), UINT(m_scratch.size()));
        } else {
            m_backend->updateTexture(m_handle, level, w, h, m_backendFormat,
                                     src.data(), UINT(src.size()));
        }
    }

    BYTE* levelPixels(UINT level) { return m_shadow[level].data(); }
    BackendHandle handle() const { return m_handle; }
    bool hasAlpha() const { return m_hasAlpha; }

private:
    IBackend* m_backend;
    UINT m_width, m_height, m_levels = 1;
    D3DFORMAT m_format;
    D3DPOOL m_pool;
    TexFormat m_backendFormat = TexFormat::RGBA8;
    BackendHandle m_handle = 0;
    bool m_hasAlpha = true;
    UINT m_lockedLevel = 0;
    std::vector<std::vector<BYTE>> m_shadow;
    std::vector<BYTE> m_scratch;
};

HRESULT Surface8::LockRect(D3DLOCKED_RECT* locked, const RECT* rect, DWORD flags) {
    if (!locked) return D3DERR_INVALIDCALL;
    if (m_owner) return m_owner->LockRect(m_level, locked, rect, flags);
    FormatInfo fi{};
    formatInfo(m_format, fi);
    locked->pBits = m_shadow.data();
    locked->Pitch = INT(m_width * (fi.bytesPerPixel ? fi.bytesPerPixel : 4));
    return D3D_OK;
}

HRESULT Surface8::UnlockRect() {
    if (m_owner) return m_owner->UnlockRect(m_level);
    return D3D_OK;
}

// ---------- device ----------

class Device8 final : public IDirect3DDevice8, public RefCounted8 {
public:
    Device8(IBackend* backend, const D3DPRESENT_PARAMETERS& pp)
        : m_backend(backend), m_pp(pp) {
        m_state.viewport = {0, 0, pp.BackBufferWidth, pp.BackBufferHeight, 0.0f, 1.0f};
        m_backend->setViewport(m_state.viewport);
    }
    ~Device8() override { delete m_backend; }

    ULONG AddRef() override { return addRef(); }
    ULONG Release() override { return release(); }

    // --- frame ---
    HRESULT TestCooperativeLevel() override { return D3D_OK; }
    UINT GetAvailableTextureMem() override { return 512u * 1024 * 1024; }
    HRESULT ResourceManagerDiscardBytes(DWORD) override { return D3D_OK; }
    HRESULT GetDeviceCaps(D3DCAPS8* caps) override { return fillCaps(caps); }

    HRESULT GetDisplayMode(D3DDISPLAYMODE* mode) override {
        if (!mode) return D3DERR_INVALIDCALL;
        *mode = {m_pp.BackBufferWidth, m_pp.BackBufferHeight, 60, D3DFMT_X8R8G8B8};
        return D3D_OK;
    }
    HRESULT Reset(D3DPRESENT_PARAMETERS* pp) override {
        if (pp) {
            m_pp = *pp;
            m_backend->resize(int(pp->BackBufferWidth), int(pp->BackBufferHeight));
        }
        return D3D_OK;
    }
    HRESULT Present(const RECT*, const RECT*, HWND, const void*) override {
        m_backend->present();
        return D3D_OK;
    }
    HRESULT GetBackBuffer(UINT, DWORD, IDirect3DSurface8** s) override {
        if (s) *s = nullptr;
        return D3DERR_NOTAVAILABLE;  // P2: needed for screenshots only
    }
    HRESULT GetFrontBuffer(IDirect3DSurface8*) override { return D3DERR_NOTAVAILABLE; }
    void SetGammaRamp(DWORD, const void*) override {}
    HRESULT CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS*, IDirect3DSwapChain8**) override {
        return D3DERR_NOTAVAILABLE;
    }
    HRESULT BeginScene() override { return D3D_OK; }
    HRESULT EndScene() override { return D3D_OK; }

    HRESULT Clear(DWORD, const void*, DWORD flags, D3DCOLOR color, float z, DWORD stencil) override {
        m_backend->clear((flags & D3DCLEAR_TARGET) != 0, (flags & D3DCLEAR_ZBUFFER) != 0,
                         (flags & D3DCLEAR_STENCIL) != 0, color, z, stencil);
        return D3D_OK;
    }

    // --- resources ---
    HRESULT CreateTexture(UINT w, UINT h, UINT levels, DWORD, D3DFORMAT format, D3DPOOL pool,
                          IDirect3DTexture8** texture) override {
        if (!texture) return D3DERR_INVALIDCALL;
        *texture = new Texture8(m_backend, w, h, levels, format, pool);
        return D3D_OK;
    }
    HRESULT CreateVertexBuffer(UINT length, DWORD usage, DWORD fvf, D3DPOOL,
                               IDirect3DVertexBuffer8** vb) override {
        if (!vb) return D3DERR_INVALIDCALL;
        *vb = new VertexBuffer8(m_backend, length, usage, fvf);
        return D3D_OK;
    }
    HRESULT CreateIndexBuffer(UINT length, DWORD usage, D3DFORMAT format, D3DPOOL,
                              IDirect3DIndexBuffer8** ib) override {
        if (!ib) return D3DERR_INVALIDCALL;
        *ib = new IndexBuffer8(m_backend, length, usage, format);
        return D3D_OK;
    }
    HRESULT CreateImageSurface(UINT w, UINT h, D3DFORMAT format, IDirect3DSurface8** s) override {
        if (!s) return D3DERR_INVALIDCALL;
        *s = new Surface8(w, h, format);
        return D3D_OK;
    }
    HRESULT CreateDepthStencilSurface(UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE,
                                      IDirect3DSurface8** s) override {
        if (s) *s = nullptr;
        return D3DERR_NOTAVAILABLE;  // P2: render-target passes
    }
    HRESULT UpdateTexture(IDirect3DBaseTexture8* src, IDirect3DBaseTexture8* dst) override {
        // Engine pattern: sysmem texture Lock/write/Unlock, then UpdateTexture to the
        // default-pool copy. Our textures upload on Unlock already; copy shadows over.
        auto* s = static_cast<Texture8*>(src);
        auto* d = static_cast<Texture8*>(dst);
        if (!s || !d) return D3DERR_INVALIDCALL;
        UINT levels = std::min(s->GetLevelCount(), d->GetLevelCount());
        for (UINT i = 0; i < levels; ++i) {
            D3DSURFACE_DESC sd{}, dd{};
            s->GetLevelDesc(i, &sd);
            d->GetLevelDesc(i, &dd);
            if (sd.Width != dd.Width || sd.Height != dd.Height || sd.Format != dd.Format) continue;
            std::memcpy(d->levelPixels(i), s->levelPixels(i), sd.Size);
            d->uploadLevel(i);
        }
        return D3D_OK;
    }
    HRESULT CopyRects(IDirect3DSurface8* srcSurf, const RECT* rects, UINT count,
                      IDirect3DSurface8* dstSurf, const POINT* points) override {
        // CPU blit between surface shadows; same-format, no stretch (D3D8 contract).
        // Used by the engine to compose glyph surfaces into sentence textures.
        auto* src = static_cast<Surface8*>(srcSurf);
        auto* dst = static_cast<Surface8*>(dstSurf);
        if (!src || !dst || src->format() != dst->format()) return D3DERR_INVALIDCALL;
        FormatInfo fi{};
        if (!formatInfo(src->format(), fi) || fi.bytesPerPixel == 0)
            return D3DERR_INVALIDCALL;  // block-compressed copies unsupported
        const UINT bpp = fi.bytesPerPixel;
        BYTE* sp = src->owner() ? src->owner()->levelPixels(src->level()) : src->pixels();
        BYTE* dp = dst->owner() ? dst->owner()->levelPixels(dst->level()) : dst->pixels();
        const UINT spitch = src->width() * bpp, dpitch = dst->width() * bpp;
        auto blit = [&](LONG sx, LONG sy, LONG w, LONG h, LONG dx, LONG dy) {
            if (sx < 0 || sy < 0 || dx < 0 || dy < 0) return;
            w = std::min({w, LONG(src->width()) - sx, LONG(dst->width()) - dx});
            h = std::min({h, LONG(src->height()) - sy, LONG(dst->height()) - dy});
            for (LONG row = 0; row < h; ++row)
                std::memcpy(dp + size_t(dy + row) * dpitch + size_t(dx) * bpp,
                            sp + size_t(sy + row) * spitch + size_t(sx) * bpp,
                            size_t(w) * bpp);
        };
        if (!rects || count == 0) {
            blit(0, 0, LONG(src->width()), LONG(src->height()), 0, 0);
        } else {
            for (UINT i = 0; i < count; ++i) {
                LONG dx = points ? points[i].x : rects[i].left;
                LONG dy = points ? points[i].y : rects[i].top;
                blit(rects[i].left, rects[i].top, rects[i].right - rects[i].left,
                     rects[i].bottom - rects[i].top, dx, dy);
            }
        }
        if (dst->owner()) dst->owner()->uploadLevel(dst->level());
        return D3D_OK;
    }
    HRESULT SetRenderTarget(IDirect3DSurface8*, IDirect3DSurface8*) override {
        return D3DERR_NOTAVAILABLE;  // P2
    }
    HRESULT GetRenderTarget(IDirect3DSurface8** rt) override {
        if (rt) *rt = nullptr;
        return D3DERR_NOTAVAILABLE;
    }
    HRESULT GetDepthStencilSurface(IDirect3DSurface8** ds) override {
        if (ds) *ds = nullptr;
        return D3DERR_NOTAVAILABLE;
    }

    // --- state ---
    HRESULT SetTransform(D3DTRANSFORMSTATETYPE t, const D3DMATRIX* m) override {
        if (!m) return D3DERR_INVALIDCALL;
        if (t == D3DTS_WORLD) m_state.world = *m;
        else if (t == D3DTS_VIEW) m_state.view = *m;
        else if (t == D3DTS_PROJECTION) m_state.projection = *m;
        // texture transforms: accepted, applied when TEXTURETRANSFORMFLAGS lands
        return D3D_OK;
    }
    HRESULT GetTransform(D3DTRANSFORMSTATETYPE t, D3DMATRIX* m) override {
        if (!m) return D3DERR_INVALIDCALL;
        if (t == D3DTS_WORLD) *m = m_state.world;
        else if (t == D3DTS_VIEW) *m = m_state.view;
        else if (t == D3DTS_PROJECTION) *m = m_state.projection;
        return D3D_OK;
    }
    HRESULT SetViewport(const D3DVIEWPORT8* vp) override {
        if (!vp) return D3DERR_INVALIDCALL;
        m_state.viewport = *vp;
        m_backend->setViewport(*vp);
        return D3D_OK;
    }
    HRESULT GetViewport(D3DVIEWPORT8* vp) override {
        if (!vp) return D3DERR_INVALIDCALL;
        *vp = m_state.viewport;
        return D3D_OK;
    }
    HRESULT SetMaterial(const D3DMATERIAL8* m) override {
        if (!m) return D3DERR_INVALIDCALL;
        m_state.material = *m;
        return D3D_OK;
    }
    HRESULT GetMaterial(D3DMATERIAL8* m) override {
        if (!m) return D3DERR_INVALIDCALL;
        *m = m_state.material;
        return D3D_OK;
    }
    HRESULT SetLight(DWORD i, const D3DLIGHT8* l) override {
        if (!l || i >= kMaxLights) return D3DERR_INVALIDCALL;
        m_state.lights[i].light = *l;
        m_state.lights[i].defined = true;
        return D3D_OK;
    }
    HRESULT LightEnable(DWORD i, BOOL enable) override {
        if (i >= kMaxLights) return D3DERR_INVALIDCALL;
        m_state.lights[i].enabled = enable != 0;
        return D3D_OK;
    }
    HRESULT GetLight(DWORD i, D3DLIGHT8* l) override {
        if (!l || i >= kMaxLights) return D3DERR_INVALIDCALL;
        *l = m_state.lights[i].light;
        return D3D_OK;
    }
    HRESULT GetLightEnable(DWORD i, BOOL* enable) override {
        if (!enable || i >= kMaxLights) return D3DERR_INVALIDCALL;
        *enable = m_state.lights[i].enabled ? TRUE : FALSE;
        return D3D_OK;
    }
    HRESULT SetClipPlane(DWORD, const float*) override { return D3D_OK; }  // P2 (water)

    HRESULT SetRenderState(D3DRENDERSTATETYPE state, DWORD value) override {
        if (DWORD(state) < D3DRS_MAX_SENTINEL) m_state.rs[state] = value;
        return D3D_OK;  // unknown states accepted silently — never crash the engine
    }
    HRESULT GetRenderState(D3DRENDERSTATETYPE state, DWORD* value) override {
        if (!value) return D3DERR_INVALIDCALL;
        *value = DWORD(state) < D3DRS_MAX_SENTINEL ? m_state.rs[state] : 0;
        return D3D_OK;
    }
    HRESULT SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value) override {
        if (stage < kMaxTextureStages && DWORD(type) < D3DTSS_MAX_SENTINEL)
            m_state.stages[stage].values[type] = value;
        return D3D_OK;
    }
    HRESULT GetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD* value) override {
        if (!value) return D3DERR_INVALIDCALL;
        *value = (stage < kMaxTextureStages && DWORD(type) < D3DTSS_MAX_SENTINEL)
                     ? m_state.stages[stage].values[type] : 0;
        return D3D_OK;
    }
    HRESULT SetTexture(DWORD stage, IDirect3DBaseTexture8* texture) override {
        if (stage >= kMaxTextureStages) return D3DERR_INVALIDCALL;
        if (!texture) {
            m_state.textures[stage] = 0;
            m_state.textureHasAlpha[stage] = false;
        } else {
            auto* t = static_cast<Texture8*>(texture);
            m_state.textures[stage] = t->handle();
            m_state.textureHasAlpha[stage] = t->hasAlpha();
        }
        return D3D_OK;
    }
    HRESULT ValidateDevice(DWORD* passes) override {
        if (passes) *passes = 1;
        return D3D_OK;
    }

    HRESULT SetVertexShader(DWORD handle) override {
        m_state.fvf = handle;  // FVF codes only (INTERFACE.md)
        return D3D_OK;
    }
    HRESULT SetVertexShaderConstant(DWORD, const void*, DWORD) override { return D3D_OK; }
    HRESULT SetPixelShader(DWORD handle) override {
        return handle == 0 ? D3D_OK : D3DERR_INVALIDCALL;  // caps say none exist
    }
    HRESULT SetPixelShaderConstant(DWORD, const void*, DWORD) override { return D3D_OK; }

    // --- geometry ---
    HRESULT SetStreamSource(UINT stream, IDirect3DVertexBuffer8* vb, UINT stride) override {
        if (stream != 0) return D3DERR_INVALIDCALL;  // stream 0 only (INTERFACE.md)
        m_vb = static_cast<VertexBuffer8*>(vb);
        m_vbStride = stride;
        return D3D_OK;
    }
    HRESULT SetIndices(IDirect3DIndexBuffer8* ib, UINT baseVertexIndex) override {
        m_ib = static_cast<IndexBuffer8*>(ib);
        m_baseVertexIndex = baseVertexIndex;
        return D3D_OK;
    }

    HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE type, UINT, UINT, UINT startIndex,
                                 UINT primCount) override {
        if (!m_vb || !m_ib) return D3DERR_INVALIDCALL;
        DrawGeometry geo;
        geo.vb = m_vb->handle();
        geo.vbStride = m_vbStride;
        geo.ib = m_ib->handle();
        geo.index32 = m_ib->index32();
        geo.baseVertexIndex = m_baseVertexIndex;
        geo.primitive = type;
        geo.startIndex = startIndex;
        geo.primCount = primCount;
        return dispatch(geo);
    }

    HRESULT DrawPrimitive(D3DPRIMITIVETYPE type, UINT startVertex, UINT primCount) override {
        if (!m_vb) return D3DERR_INVALIDCALL;
        DrawGeometry geo;
        geo.vb = m_vb->handle();
        geo.vbStride = m_vbStride;
        geo.primitive = type;
        geo.startVertex = startVertex;
        geo.primCount = primCount;
        return dispatch(geo);
    }

    HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE type, UINT primCount,
                            const void* vertexData, UINT stride) override {
        if (!vertexData || !stride) return D3DERR_INVALIDCALL;
        UINT verts = vertexCountFor(type, primCount);
        BackendHandle vb = scratchVB(vertexData, verts * stride);
        DrawGeometry geo;
        geo.vb = vb;
        geo.vbStride = stride;
        geo.primitive = type;
        geo.primCount = primCount;
        return dispatch(geo);
    }

    HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE type, UINT, UINT numVertices, UINT primCount,
                                   const void* indexData, D3DFORMAT indexFormat,
                                   const void* vertexData, UINT stride) override {
        if (!vertexData || !indexData || !stride) return D3DERR_INVALIDCALL;
        BackendHandle vb = scratchVB(vertexData, numVertices * stride);
        bool i32 = indexFormat == D3DFMT_INDEX32;
        UINT idxCount = vertexCountFor(type, primCount);
        BackendHandle ib = scratchIB(indexData, idxCount * (i32 ? 4u : 2u), i32);
        DrawGeometry geo;
        geo.vb = vb;
        geo.vbStride = stride;
        geo.ib = ib;
        geo.index32 = i32;
        geo.primitive = type;
        geo.primCount = primCount;
        return dispatch(geo);
    }

private:
    static UINT vertexCountFor(D3DPRIMITIVETYPE t, UINT prims) {
        switch (t) {
            case D3DPT_POINTLIST: return prims;
            case D3DPT_LINELIST: return prims * 2;
            case D3DPT_LINESTRIP: return prims + 1;
            case D3DPT_TRIANGLELIST: return prims * 3;
            case D3DPT_TRIANGLESTRIP: return prims + 2;
            case D3DPT_TRIANGLEFAN: return prims + 2;
        }
        return 0;
    }

    HRESULT dispatch(DrawGeometry& geo) {
        DWORD fvf = m_state.fvf ? m_state.fvf : (m_vb ? m_vb->fvf() : 0);
        if (!fvf) return D3DERR_INVALIDCALL;
        geo.layout = VertexLayout::fromFVF(fvf);
        if (!geo.vbStride) geo.vbStride = geo.layout.stride;
        ShaderKey key = ShaderKey::from(m_state, geo.layout);
        m_backend->draw(geo, m_state, key);
        return D3D_OK;
    }

    BackendHandle scratchVB(const void* data, UINT size) {
        if (size > m_scratchVBSize) {
            if (m_scratchVB) m_backend->destroyBuffer(m_scratchVB);
            m_scratchVBSize = std::max(size, 64u * 1024);
            m_scratchVB = m_backend->createBuffer(BufferKind::Vertex, m_scratchVBSize, true);
        }
        m_backend->updateBuffer(m_scratchVB, 0, data, size, true);
        return m_scratchVB;
    }
    BackendHandle scratchIB(const void* data, UINT size, bool i32) {
        if (size > m_scratchIBSize) {
            if (m_scratchIB) m_backend->destroyBuffer(m_scratchIB);
            m_scratchIBSize = std::max(size, 16u * 1024);
            m_scratchIB = m_backend->createBuffer(i32 ? BufferKind::Index32 : BufferKind::Index16,
                                                  m_scratchIBSize, true);
        }
        m_backend->updateBuffer(m_scratchIB, 0, data, size, true);
        return m_scratchIB;
    }

    HRESULT fillCaps(D3DCAPS8* caps) {
        if (!caps) return D3DERR_INVALIDCALL;
        *caps = {};
        caps->DeviceType = D3DDEVTYPE_HAL;
        caps->MaxTextureWidth = 4096;
        caps->MaxTextureHeight = 4096;
        caps->MaxTextureBlendStages = kActiveStages;
        caps->MaxSimultaneousTextures = kActiveStages;
        caps->MaxActiveLights = kMaxLights;
        caps->MaxUserClipPlanes = 0;
        caps->MaxPrimitiveCount = 1u << 20;
        caps->MaxVertexIndex = 1u << 20;
        caps->MaxStreams = 1;
        caps->MaxStreamStride = 256;
        caps->VertexShaderVersion = 0;  // force FFP paths (INTERFACE.md)
        caps->PixelShaderVersion = 0;
        return D3D_OK;
    }

    IBackend* m_backend;
    D3DPRESENT_PARAMETERS m_pp;
    StateSnapshot m_state;
    VertexBuffer8* m_vb = nullptr;
    IndexBuffer8* m_ib = nullptr;
    UINT m_vbStride = 0;
    UINT m_baseVertexIndex = 0;
    BackendHandle m_scratchVB = 0, m_scratchIB = 0;
    UINT m_scratchVBSize = 0, m_scratchIBSize = 0;
};

// ---------- IDirect3D8 ----------

class Direct3D8 final : public IDirect3D8, public RefCounted8 {
public:
    ULONG AddRef() override { return addRef(); }
    ULONG Release() override { return release(); }

    UINT GetAdapterCount() override { return 1; }
    HRESULT GetAdapterIdentifier(UINT, DWORD, D3DADAPTER_IDENTIFIER8* id) override {
        if (!id) return D3DERR_INVALIDCALL;
        *id = {};
        std::snprintf(id->Driver, sizeof(id->Driver), "d8web");
        std::snprintf(id->Description, sizeof(id->Description), "d8web WebGL2 adapter");
        id->VendorId = 0xD8;
        return D3D_OK;
    }
    UINT GetAdapterModeCount(UINT) override { return 1; }
    HRESULT EnumAdapterModes(UINT, UINT, D3DDISPLAYMODE* mode) override {
        return GetAdapterDisplayMode(0, mode);
    }
    HRESULT GetAdapterDisplayMode(UINT, D3DDISPLAYMODE* mode) override {
        if (!mode) return D3DERR_INVALIDCALL;
        *mode = {1920, 1080, 60, D3DFMT_X8R8G8B8};
        return D3D_OK;
    }
    HRESULT CheckDeviceFormat(UINT, D3DDEVTYPE, D3DFORMAT, DWORD, D3DRESOURCETYPE,
                              D3DFORMAT checkFormat) override {
        FormatInfo fi;
        if (formatInfo(checkFormat, fi)) return D3D_OK;
        if (checkFormat == D3DFMT_D16 || checkFormat == D3DFMT_D24S8) return D3D_OK;
        return D3DERR_NOTAVAILABLE;
    }
    HRESULT CheckDepthStencilMatch(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, D3DFORMAT) override {
        return D3D_OK;
    }
    HRESULT GetDeviceCaps(UINT, D3DDEVTYPE, D3DCAPS8* caps) override {
        // Same caps the device reports; a throwaway device isn't needed
        if (!caps) return D3DERR_INVALIDCALL;
        *caps = {};
        caps->DeviceType = D3DDEVTYPE_HAL;
        caps->MaxTextureWidth = 4096;
        caps->MaxTextureHeight = 4096;
        caps->MaxTextureBlendStages = kActiveStages;
        caps->MaxSimultaneousTextures = kActiveStages;
        caps->MaxActiveLights = kMaxLights;
        caps->MaxStreams = 1;
        caps->VertexShaderVersion = 0;
        caps->PixelShaderVersion = 0;
        return D3D_OK;
    }
    HRESULT CreateDevice(UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS* pp,
                         IDirect3DDevice8** device) override {
        if (!pp || !device) return D3DERR_INVALIDCALL;
        IBackend* backend = createWebGL2Backend();
        if (!backend->init(int(pp->BackBufferWidth), int(pp->BackBufferHeight))) {
            delete backend;
            return D3DERR_NOTAVAILABLE;
        }
        *device = new Device8(backend, *pp);
        return D3D_OK;
    }
};

}  // namespace

IDirect3D8* CreateDirect3D8() { return new Direct3D8(); }

}  // namespace d8web
