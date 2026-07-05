// d8web example 01 — rotating lit cube through the D3D8 API.
// Written the way a 2002 codebase would use D3D8: FVF vertices, Lock/Unlock,
// SetRenderState, a directional light, DrawIndexedPrimitive. If this renders,
// the whole path frontend → seam → WebGL2 backend works.

#include <d8web/d3d8.h>

using namespace d8web;

#include <cmath>
#include <cstdio>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace {

constexpr DWORD kCubeFVF = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE;

struct CubeVertex {
    float x, y, z;
    float nx, ny, nz;
    D3DCOLOR color;
};

IDirect3DDevice8* g_device = nullptr;
IDirect3DVertexBuffer8* g_vb = nullptr;
IDirect3DIndexBuffer8* g_ib = nullptr;
float g_angle = 0.0f;

void makeIdentity(D3DMATRIX& m) {
    m = {};
    m._11 = m._22 = m._33 = m._44 = 1.0f;
}

// Left-handed, like D3DX did it
D3DMATRIX rotationY(float a) {
    D3DMATRIX m;
    makeIdentity(m);
    m._11 = std::cos(a); m._13 = -std::sin(a);
    m._31 = std::sin(a); m._33 = std::cos(a);
    return m;
}

D3DMATRIX rotationX(float a) {
    D3DMATRIX m;
    makeIdentity(m);
    m._22 = std::cos(a); m._23 = std::sin(a);
    m._32 = -std::sin(a); m._33 = std::cos(a);
    return m;
}

D3DMATRIX multiply(const D3DMATRIX& a, const D3DMATRIX& b) {
    D3DMATRIX r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 4; ++k) r.m[i][j] += a.m[i][k] * b.m[k][j];
    return r;
}

D3DMATRIX lookAtLH(float eyeZ) {
    // Eye at (0,0,eyeZ) looking at origin, up +Y — reduces to a translation
    D3DMATRIX m;
    makeIdentity(m);
    m._43 = -eyeZ;
    return m;
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

bool createScene() {
    // 24 vertices (per-face normals), 36 indices
    const D3DCOLOR C[6] = {
        D3DCOLOR_XRGB(200, 60, 60), D3DCOLOR_XRGB(60, 200, 60), D3DCOLOR_XRGB(60, 60, 200),
        D3DCOLOR_XRGB(200, 200, 60), D3DCOLOR_XRGB(60, 200, 200), D3DCOLOR_XRGB(200, 60, 200),
    };
    const float N[6][3] = {{0, 0, -1}, {0, 0, 1}, {0, -1, 0}, {0, 1, 0}, {-1, 0, 0}, {1, 0, 0}};
    const float Q[6][4][3] = {
        {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1}},   // front  (-Z)
        {{1, -1, 1}, {-1, -1, 1}, {-1, 1, 1}, {1, 1, 1}},       // back   (+Z)
        {{-1, -1, 1}, {1, -1, 1}, {1, -1, -1}, {-1, -1, -1}},   // bottom (-Y)
        {{-1, 1, -1}, {1, 1, -1}, {1, 1, 1}, {-1, 1, 1}},       // top    (+Y)
        {{-1, -1, 1}, {-1, -1, -1}, {-1, 1, -1}, {-1, 1, 1}},   // left   (-X)
        {{1, -1, -1}, {1, -1, 1}, {1, 1, 1}, {1, 1, -1}},       // right  (+X)
    };

    CubeVertex verts[24];
    WORD indices[36];
    for (int f = 0; f < 6; ++f) {
        for (int v = 0; v < 4; ++v) {
            CubeVertex& cv = verts[f * 4 + v];
            cv.x = Q[f][v][0]; cv.y = Q[f][v][1]; cv.z = Q[f][v][2];
            cv.nx = N[f][0]; cv.ny = N[f][1]; cv.nz = N[f][2];
            cv.color = C[f];
        }
        WORD b = WORD(f * 4);
        WORD* idx = indices + f * 6;
        // Clockwise-when-facing-camera winding — D3D8 front faces with D3DCULL_CCW.
        // (Quads above are listed CCW, so emit each triangle reversed.)
        idx[0] = b; idx[1] = WORD(b + 2); idx[2] = WORD(b + 1);
        idx[3] = b; idx[4] = WORD(b + 3); idx[5] = WORD(b + 2);
    }

    if (FAILED(g_device->CreateVertexBuffer(sizeof(verts), 0, kCubeFVF, D3DPOOL_MANAGED, &g_vb)))
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

    // Fixed-function state, vintage 2002
    g_device->SetRenderState(D3DRS_LIGHTING, TRUE);
    g_device->SetRenderState(D3DRS_AMBIENT, D3DCOLOR_XRGB(40, 40, 48));
    g_device->SetRenderState(D3DRS_ZENABLE, TRUE);
    g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);

    D3DLIGHT8 sun{};
    sun.Type = D3DLIGHT_DIRECTIONAL;
    sun.Diffuse = {1.0f, 0.95f, 0.85f, 1.0f};
    // Biased toward the camera (-Z side) so visible faces stay lit while rotating
    sun.Direction = {-0.3f, -0.5f, 0.8f};
    g_device->SetLight(0, &sun);
    g_device->LightEnable(0, TRUE);

    D3DMATERIAL8 mat{};
    mat.Diffuse = {1, 1, 1, 1};
    mat.Ambient = {1, 1, 1, 1};
    g_device->SetMaterial(&mat);

    D3DMATRIX view = lookAtLH(-5.0f);
    g_device->SetTransform(D3DTS_VIEW, &view);
    D3DMATRIX proj = perspectiveFovLH(3.14159f / 4.0f, 16.0f / 9.0f, 0.1f, 100.0f);
    g_device->SetTransform(D3DTS_PROJECTION, &proj);
    return true;
}

void frame() {
    g_angle += 0.015f;
#ifdef __EMSCRIPTEN__
    // Diagnostic pose: open cube.html#freeze to render with identity world matrix
    static const bool freeze = EM_ASM_INT({ return location.hash.indexOf('freeze') >= 0 ? 1 : 0; }) != 0;
#else
    static const bool freeze = false;
#endif
    D3DMATRIX world;
    if (freeze) makeIdentity(world);
    else world = multiply(rotationX(g_angle * 0.6f), rotationY(g_angle));
    g_device->SetTransform(D3DTS_WORLD, &world);

    g_device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                    D3DCOLOR_XRGB(24, 26, 33), 1.0f, 0);
    g_device->BeginScene();
    g_device->SetVertexShader(kCubeFVF);
    g_device->SetStreamSource(0, g_vb, sizeof(CubeVertex));
    g_device->SetIndices(g_ib, 0);
    g_device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 24, 0, 12);
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
    std::printf("d8web cube demo: device up, entering main loop\n");

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(frame, 0, 1);
#else
    for (int i = 0; i < 3; ++i) frame();  // native smoke run
#endif
    return 0;
}
