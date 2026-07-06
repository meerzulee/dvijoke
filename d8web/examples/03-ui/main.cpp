// d8web example 03 — pre-transformed (XYZRHW) UI rendering through the D3D8 API.
// This is how SAGE draws every menu and HUD element: screen-space vertices, no
// transforms, alpha blending. Exercises the XYZRHW vertex path, alpha blending
// (untested by examples 01/02), depth-disabled draws, and texture + blend combined.
//
// Scene, drawn painter's-order like a real 2000s UI:
//   1. opaque background "scene" strip (textured checker, pretending to be gameplay)
//   2. translucent dark panel over it (alpha blend)
//   3. three "buttons", middle one pulsing (vertex alpha animation)
//   4. textured "icon" with blend enabled

#include <d8web/d3d8.h>

using namespace d8web;

#include <cmath>
#include <cstdio>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace {

constexpr DWORD kUIFVF = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

struct UIVertex {
    float x, y, z, rhw;
    D3DCOLOR color;
    float u, v;
};

IDirect3DDevice8* g_device = nullptr;
IDirect3DTexture8* g_checker = nullptr;
float g_time = 0.0f;

IDirect3DTexture8* makeCheckerTexture() {
    IDirect3DTexture8* tex = nullptr;
    if (FAILED(g_device->CreateTexture(64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex)))
        return nullptr;
    D3DLOCKED_RECT lr{};
    tex->LockRect(0, &lr, nullptr, 0);
    auto* px = static_cast<DWORD*>(lr.pBits);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            px[y * 64 + x] = (((x / 8) + (y / 8)) & 1) ? D3DCOLOR_ARGB(255, 90, 140, 60)
                                                       : D3DCOLOR_ARGB(255, 50, 90, 40);
    tex->UnlockRect(0);
    return tex;
}

// Screen-space rect as two triangles; z=0.5, rhw=1 (classic UI values)
void drawRect(float x0, float y0, float x1, float y1, D3DCOLOR c,
              IDirect3DTexture8* tex = nullptr) {
    const UIVertex v[6] = {
        {x0, y0, 0.5f, 1.0f, c, 0.0f, 0.0f},
        {x1, y0, 0.5f, 1.0f, c, 1.0f, 0.0f},
        {x1, y1, 0.5f, 1.0f, c, 1.0f, 1.0f},
        {x0, y0, 0.5f, 1.0f, c, 0.0f, 0.0f},
        {x1, y1, 0.5f, 1.0f, c, 1.0f, 1.0f},
        {x0, y1, 0.5f, 1.0f, c, 0.0f, 1.0f},
    };
    g_device->SetTexture(0, tex);
    g_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(UIVertex));
}

void frame() {
    g_time += 1.0f / 60.0f;

    g_device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(16, 17, 22), 1.0f, 0);
    g_device->BeginScene();
    g_device->SetVertexShader(kUIFVF);

    // 1. Opaque "gameplay" strip: textured, no blend
    g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    drawRect(0, 0, 1280, 720, D3DCOLOR_XRGB(255, 255, 255), g_checker);

    // 2. Translucent panel: classic menu overlay (alpha blend, vertex alpha 180)
    g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    g_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    g_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    drawRect(340, 120, 940, 600, D3DCOLOR_ARGB(185, 12, 14, 22));

    // 3. Buttons: top static, middle pulsing via vertex alpha, bottom static
    drawRect(420, 180, 860, 260, D3DCOLOR_ARGB(230, 196, 148, 58));
    BYTE pulse = BYTE(150 + 100 * std::sin(g_time * 3.0f));
    drawRect(420, 300, 860, 380, D3DCOLOR_ARGB(pulse, 88, 160, 96));
    drawRect(420, 420, 860, 500, D3DCOLOR_ARGB(230, 70, 96, 150));

    // 4. Textured icon with blend + vertex tint (texture × diffuse, stage-0 default)
    drawRect(660, 520, 740, 584, D3DCOLOR_ARGB(255, 255, 220, 180), g_checker);

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

    g_checker = makeCheckerTexture();
    if (!g_checker) {
        std::fprintf(stderr, "texture setup failed\n");
        return 1;
    }
    // UI state: no lighting, no depth, no cull — SAGE menu defaults
    g_device->SetRenderState(D3DRS_LIGHTING, FALSE);
    g_device->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

    std::printf("d8web ui demo: entering main loop\n");

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(frame, 0, 1);
#else
    for (int i = 0; i < 3; ++i) frame();
#endif
    return 0;
}
