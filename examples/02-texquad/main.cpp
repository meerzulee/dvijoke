// d8web example 02 — textured quads through the D3D8 API.
// Exercises what the cube demo doesn't: CreateTexture + LockRect/UnlockRect upload
// (with ARGB→RGBA swizzle), SetTexture + stage-0 MODULATE defaults, UV attributes,
// alpha blending, alpha test (discard), and the DrawPrimitiveUP scratch-buffer path.
//
// Scene: left quad = checkerboard modulated by a vertex-color gradient (static VB path);
// right quad = "fence" texture with alpha holes, alpha-tested, drawn via
// DrawPrimitiveUP, slowly swaying.

#include <d8web/d3d8.h>

#include <cmath>
#include <cstdio>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace {

constexpr DWORD kQuadFVF = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;

struct QuadVertex {
    float x, y, z;
    D3DCOLOR color;
    float u, v;
};

IDirect3DDevice8* g_device = nullptr;
IDirect3DVertexBuffer8* g_vb = nullptr;
IDirect3DIndexBuffer8* g_ib = nullptr;
IDirect3DTexture8* g_checker = nullptr;
IDirect3DTexture8* g_fence = nullptr;
float g_time = 0.0f;

void makeIdentity(D3DMATRIX& m) {
    m = {};
    m._11 = m._22 = m._33 = m._44 = 1.0f;
}

D3DMATRIX perspectiveFovLH(float fovY, float aspect, float zn, float zf) {
    float ys = 1.0f / std::tan(fovY * 0.5f);
    float xs = ys / aspect;
    D3DMATRIX m{};
    m._11 = xs;
    m._22 = ys;
    m._33 = zf / (zf - zn);
    m._34 = 1.0f;
    m._43 = -zn * zf / (zf - zn);
    return m;
}

// 128x128 checkerboard, A8R8G8B8 — tests the CPU BGRA→RGBA swizzle on upload
IDirect3DTexture8* makeCheckerTexture() {
    IDirect3DTexture8* tex = nullptr;
    if (FAILED(g_device->CreateTexture(128, 128, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex)))
        return nullptr;
    D3DLOCKED_RECT lr{};
    tex->LockRect(0, &lr, nullptr, 0);
    auto* pixels = static_cast<DWORD*>(lr.pBits);
    for (int y = 0; y < 128; ++y) {
        for (int x = 0; x < 128; ++x) {
            bool on = ((x / 16) + (y / 16)) & 1;
            // Orange/teal checker — asymmetric colors expose channel swaps instantly
            pixels[y * 128 + x] = on ? D3DCOLOR_ARGB(255, 230, 130, 30)
                                     : D3DCOLOR_ARGB(255, 20, 130, 140);
        }
    }
    tex->UnlockRect(0);
    return tex;
}

// 128x128 "fence": opaque diagonal lattice, transparent holes — tests alpha test
IDirect3DTexture8* makeFenceTexture() {
    IDirect3DTexture8* tex = nullptr;
    if (FAILED(g_device->CreateTexture(128, 128, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex)))
        return nullptr;
    D3DLOCKED_RECT lr{};
    tex->LockRect(0, &lr, nullptr, 0);
    auto* pixels = static_cast<DWORD*>(lr.pBits);
    for (int y = 0; y < 128; ++y) {
        for (int x = 0; x < 128; ++x) {
            bool bar = (x % 32) < 6 || (y % 32) < 6;
            pixels[y * 128 + x] = bar ? D3DCOLOR_ARGB(255, 190, 190, 200)
                                      : D3DCOLOR_ARGB(0, 0, 0, 0);  // hole
        }
    }
    tex->UnlockRect(0);
    return tex;
}

bool createScene() {
    // Left quad: static VB/IB, vertex-color gradient white→sky-blue
    const QuadVertex verts[4] = {
        {-2.2f, -1.0f, 0.0f, D3DCOLOR_XRGB(255, 255, 255), 0.0f, 1.0f},
        {-0.2f, -1.0f, 0.0f, D3DCOLOR_XRGB(255, 255, 255), 1.0f, 1.0f},
        {-0.2f, 1.0f, 0.0f, D3DCOLOR_XRGB(120, 170, 255), 1.0f, 0.0f},
        {-2.2f, 1.0f, 0.0f, D3DCOLOR_XRGB(120, 170, 255), 0.0f, 0.0f},
    };
    const WORD indices[6] = {0, 2, 1, 0, 3, 2};  // clockwise in D3D terms

    if (FAILED(g_device->CreateVertexBuffer(sizeof(verts), 0, kQuadFVF, D3DPOOL_MANAGED, &g_vb)))
        return false;
    BYTE* p = nullptr;
    g_vb->Lock(0, 0, &p, 0);
    std::memcpy(p, verts, sizeof(verts));
    g_vb->Unlock();

    if (FAILED(g_device->CreateIndexBuffer(sizeof(indices), 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &g_ib)))
        return false;
    g_ib->Lock(0, 0, &p, 0);
    std::memcpy(p, indices, sizeof(indices));
    g_ib->Unlock();

    g_checker = makeCheckerTexture();
    g_fence = makeFenceTexture();
    if (!g_checker || !g_fence) return false;

    // Untextured-era state: no lighting, vertex color feeds stage-0 MODULATE
    g_device->SetRenderState(D3DRS_LIGHTING, FALSE);
    g_device->SetRenderState(D3DRS_ZENABLE, TRUE);
    g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);  // quads visible both sides

    D3DMATRIX identity;
    makeIdentity(identity);
    g_device->SetTransform(D3DTS_VIEW, &identity);  // camera at origin looking +Z
    D3DMATRIX proj = perspectiveFovLH(3.14159f / 4.0f, 16.0f / 9.0f, 0.1f, 100.0f);
    g_device->SetTransform(D3DTS_PROJECTION, &proj);
    return true;
}

void frame() {
    g_time += 1.0f / 60.0f;

    g_device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                    D3DCOLOR_XRGB(28, 30, 38), 1.0f, 0);
    g_device->BeginScene();
    g_device->SetVertexShader(kQuadFVF);

    // Both quads live at z=4 in front of the origin camera
    D3DMATRIX world;
    makeIdentity(world);
    world._43 = 4.0f;
    g_device->SetTransform(D3DTS_WORLD, &world);

    // --- Left: checkerboard, static VB path, stage-0 MODULATE with vertex gradient ---
    g_device->SetTexture(0, g_checker);
    g_device->SetStreamSource(0, g_vb, sizeof(QuadVertex));
    g_device->SetIndices(g_ib, 0);
    g_device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 4, 0, 2);

    // --- Right: alpha-tested fence via DrawPrimitiveUP, swaying ---
    float sway = std::sin(g_time * 1.3f) * 0.35f;
    const QuadVertex fence[6] = {
        // Triangle list (two tris), white vertex color = texture as-is
        {0.2f + sway * 0.2f, -1.0f, 0.0f, D3DCOLOR_XRGB(255, 255, 255), 0.0f, 1.0f},
        {0.2f + sway, 1.0f, 0.0f, D3DCOLOR_XRGB(255, 255, 255), 0.0f, 0.0f},
        {2.2f + sway, 1.0f, 0.0f, D3DCOLOR_XRGB(255, 255, 255), 1.0f, 0.0f},
        {0.2f + sway * 0.2f, -1.0f, 0.0f, D3DCOLOR_XRGB(255, 255, 255), 0.0f, 1.0f},
        {2.2f + sway, 1.0f, 0.0f, D3DCOLOR_XRGB(255, 255, 255), 1.0f, 0.0f},
        {2.2f + sway * 0.2f, -1.0f, 0.0f, D3DCOLOR_XRGB(255, 255, 255), 1.0f, 1.0f},
    };
    g_device->SetTexture(0, g_fence);
    g_device->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
    g_device->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
    g_device->SetRenderState(D3DRS_ALPHAREF, 128);
    g_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, fence, sizeof(QuadVertex));
    g_device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);

    g_device->EndScene();
    g_device->Present(nullptr, nullptr, nullptr, nullptr);
}

}  // namespace

int main() {
    IDirect3D8* d3d = Direct3DCreate8(D3D_SDK_VERSION);

    D3DPRESENT_PARAMETERS pp{};
    pp.BackBufferWidth = 1280;
    pp.BackBufferHeight = 720;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;

    if (FAILED(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, nullptr,
                                 D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &g_device))) {
        std::fprintf(stderr, "CreateDevice failed\n");
        return 1;
    }
    if (!createScene()) {
        std::fprintf(stderr, "scene setup failed\n");
        return 1;
    }
    std::printf("d8web texquad demo: textures uploaded, entering main loop\n");

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(frame, 0, 1);
#else
    for (int i = 0; i < 3; ++i) frame();
#endif
    return 0;
}
