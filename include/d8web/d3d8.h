// d8web — D3D8 interface declarations (engine-facing)
// Plain C++ virtual interfaces mirroring IDirect3D8 signatures for the surface in
// INTERFACE.md. No COM machinery beyond AddRef/Release refcounts.
#pragma once

#include "d3d8types.h"

namespace d8web {

struct IDirect3D8;
struct IDirect3DDevice8;
struct IDirect3DTexture8;
struct IDirect3DSurface8;
struct IDirect3DVertexBuffer8;
struct IDirect3DIndexBuffer8;
struct IDirect3DBaseTexture8;
struct IDirect3DSwapChain8;

struct IUnknown8 {
    virtual ~IUnknown8() = default;
    virtual ULONG AddRef() = 0;
    virtual ULONG Release() = 0;
};

struct IDirect3DBaseTexture8 : IUnknown8 {
    virtual D3DRESOURCETYPE GetType() = 0;
};

struct IDirect3DSurface8 : IUnknown8 {
    virtual HRESULT GetDesc(D3DSURFACE_DESC* desc) = 0;
    virtual HRESULT LockRect(D3DLOCKED_RECT* locked, const RECT* rect, DWORD flags) = 0;
    virtual HRESULT UnlockRect() = 0;
};

struct IDirect3DTexture8 : IDirect3DBaseTexture8 {
    virtual DWORD GetLevelCount() = 0;
    virtual HRESULT GetLevelDesc(UINT level, D3DSURFACE_DESC* desc) = 0;
    virtual HRESULT GetSurfaceLevel(UINT level, IDirect3DSurface8** surface) = 0;
    virtual HRESULT LockRect(UINT level, D3DLOCKED_RECT* locked, const RECT* rect, DWORD flags) = 0;
    virtual HRESULT UnlockRect(UINT level) = 0;
    virtual DWORD SetPriority(DWORD priority) = 0;
    virtual DWORD GetPriority() = 0;
};

struct IDirect3DVertexBuffer8 : IUnknown8 {
    virtual HRESULT Lock(UINT offset, UINT size, BYTE** data, DWORD flags) = 0;
    virtual HRESULT Unlock() = 0;
};

struct IDirect3DIndexBuffer8 : IUnknown8 {
    virtual HRESULT Lock(UINT offset, UINT size, BYTE** data, DWORD flags) = 0;
    virtual HRESULT Unlock() = 0;
};

struct IDirect3DDevice8 : IUnknown8 {
    // Frame & swap
    virtual HRESULT TestCooperativeLevel() = 0;
    virtual UINT GetAvailableTextureMem() = 0;
    virtual HRESULT ResourceManagerDiscardBytes(DWORD bytes) = 0;
    virtual HRESULT GetDeviceCaps(D3DCAPS8* caps) = 0;
    virtual HRESULT GetDisplayMode(D3DDISPLAYMODE* mode) = 0;
    virtual HRESULT Reset(D3DPRESENT_PARAMETERS* pp) = 0;
    virtual HRESULT Present(const RECT* src, const RECT* dst, HWND wnd, const void* dirty) = 0;
    virtual HRESULT GetBackBuffer(UINT index, DWORD type, IDirect3DSurface8** surface) = 0;
    virtual HRESULT GetFrontBuffer(IDirect3DSurface8* dest) = 0;
    virtual void SetGammaRamp(DWORD flags, const void* ramp) = 0;
    virtual HRESULT CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS* pp, IDirect3DSwapChain8** swap) = 0;
    virtual HRESULT BeginScene() = 0;
    virtual HRESULT EndScene() = 0;
    virtual HRESULT Clear(DWORD count, const void* rects, DWORD flags,
                          D3DCOLOR color, float z, DWORD stencil) = 0;

    // Resources
    virtual HRESULT CreateTexture(UINT width, UINT height, UINT levels, DWORD usage,
                                  D3DFORMAT format, D3DPOOL pool, IDirect3DTexture8** texture) = 0;
    virtual HRESULT CreateVertexBuffer(UINT length, DWORD usage, DWORD fvf, D3DPOOL pool,
                                       IDirect3DVertexBuffer8** vb) = 0;
    virtual HRESULT CreateIndexBuffer(UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool,
                                      IDirect3DIndexBuffer8** ib) = 0;
    virtual HRESULT CreateImageSurface(UINT width, UINT height, D3DFORMAT format,
                                       IDirect3DSurface8** surface) = 0;
    virtual HRESULT CreateDepthStencilSurface(UINT width, UINT height, D3DFORMAT format,
                                              D3DMULTISAMPLE_TYPE ms, IDirect3DSurface8** surface) = 0;
    virtual HRESULT UpdateTexture(IDirect3DBaseTexture8* src, IDirect3DBaseTexture8* dst) = 0;
    virtual HRESULT CopyRects(IDirect3DSurface8* src, const RECT* srcRects, UINT count,
                              IDirect3DSurface8* dst, const POINT* dstPoints) = 0;
    virtual HRESULT SetRenderTarget(IDirect3DSurface8* rt, IDirect3DSurface8* ds) = 0;
    virtual HRESULT GetRenderTarget(IDirect3DSurface8** rt) = 0;
    virtual HRESULT GetDepthStencilSurface(IDirect3DSurface8** ds) = 0;

    // State
    virtual HRESULT SetTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX* matrix) = 0;
    virtual HRESULT GetTransform(D3DTRANSFORMSTATETYPE state, D3DMATRIX* matrix) = 0;
    virtual HRESULT SetViewport(const D3DVIEWPORT8* viewport) = 0;
    virtual HRESULT SetMaterial(const D3DMATERIAL8* material) = 0;
    virtual HRESULT SetLight(DWORD index, const D3DLIGHT8* light) = 0;
    virtual HRESULT LightEnable(DWORD index, BOOL enable) = 0;
    virtual HRESULT SetClipPlane(DWORD index, const float* plane) = 0;
    virtual HRESULT SetRenderState(D3DRENDERSTATETYPE state, DWORD value) = 0;
    virtual HRESULT SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value) = 0;
    virtual HRESULT SetTexture(DWORD stage, IDirect3DBaseTexture8* texture) = 0;
    virtual HRESULT ValidateDevice(DWORD* passes) = 0;

    // Shaders (FVF only — see INTERFACE.md)
    virtual HRESULT SetVertexShader(DWORD handle) = 0;
    virtual HRESULT SetVertexShaderConstant(DWORD reg, const void* data, DWORD count) = 0;
    virtual HRESULT SetPixelShader(DWORD handle) = 0;
    virtual HRESULT SetPixelShaderConstant(DWORD reg, const void* data, DWORD count) = 0;

    // Geometry
    virtual HRESULT SetStreamSource(UINT stream, IDirect3DVertexBuffer8* vb, UINT stride) = 0;
    virtual HRESULT SetIndices(IDirect3DIndexBuffer8* ib, UINT baseVertexIndex) = 0;
    virtual HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE type, UINT minIndex, UINT numVertices,
                                         UINT startIndex, UINT primCount) = 0;
    virtual HRESULT DrawPrimitive(D3DPRIMITIVETYPE type, UINT startVertex, UINT primCount) = 0;
    virtual HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE type, UINT primCount,
                                    const void* vertexData, UINT stride) = 0;
    virtual HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE type, UINT minIndex, UINT numVertices,
                                           UINT primCount, const void* indexData, D3DFORMAT indexFormat,
                                           const void* vertexData, UINT stride) = 0;
};

struct IDirect3D8 : IUnknown8 {
    virtual UINT GetAdapterCount() = 0;
    virtual HRESULT GetAdapterIdentifier(UINT adapter, DWORD flags, D3DADAPTER_IDENTIFIER8* id) = 0;
    virtual UINT GetAdapterModeCount(UINT adapter) = 0;
    virtual HRESULT EnumAdapterModes(UINT adapter, UINT index, D3DDISPLAYMODE* mode) = 0;
    virtual HRESULT GetAdapterDisplayMode(UINT adapter, D3DDISPLAYMODE* mode) = 0;
    virtual HRESULT CheckDeviceFormat(UINT adapter, D3DDEVTYPE type, D3DFORMAT adapterFormat,
                                      DWORD usage, D3DRESOURCETYPE rtype, D3DFORMAT checkFormat) = 0;
    virtual HRESULT CheckDepthStencilMatch(UINT adapter, D3DDEVTYPE type, D3DFORMAT adapterFormat,
                                           D3DFORMAT rtFormat, D3DFORMAT dsFormat) = 0;
    virtual HRESULT GetDeviceCaps(UINT adapter, D3DDEVTYPE type, D3DCAPS8* caps) = 0;
    virtual HRESULT CreateDevice(UINT adapter, D3DDEVTYPE type, HWND wnd, DWORD behaviorFlags,
                                 D3DPRESENT_PARAMETERS* pp, IDirect3DDevice8** device) = 0;
};

// Entry point
IDirect3D8* CreateDirect3D8();

}  // namespace d8web

// Global factory mirroring Direct3DCreate8(D3D_SDK_VERSION) for standalone use.
// The engine bridge defines D8WEB_NO_GLOBAL_FACTORY and provides the real
// COM-shaped Direct3DCreate8 itself.
#ifndef D8WEB_NO_GLOBAL_FACTORY
inline d8web::IDirect3D8* Direct3DCreate8(d8web::UINT) { return d8web::CreateDirect3D8(); }
#endif
