// d8web core — device state snapshot
// The frontend records every D3D8 state change here; backends read the snapshot at
// draw time. No backend API types may appear in this file — this is the seam.
#pragma once

#include "d8web/d3d8types.h"

#include <array>
#include <cstdint>

namespace d8web {

constexpr int kMaxTextureStages = 8;   // accepted; backends honor kActiveStages
constexpr int kActiveStages = 2;       // honored initially (terrain multitexture)
constexpr int kMaxLights = 8;

struct StageState {
    std::array<DWORD, D3DTSS_MAX_SENTINEL> values{};
    // D3D8 defaults: stage 0 is MODULATE/SELECTARG1 (texturing works without any
    // SetTextureStageState calls); stages >= 1 are DISABLE. The frontend downgrades
    // this struct's stage-0 defaults for higher stages at device creation.
    StageState() {
        values[D3DTSS_COLOROP] = D3DTOP_MODULATE;
        values[D3DTSS_COLORARG1] = D3DTA_TEXTURE;
        values[D3DTSS_COLORARG2] = D3DTA_CURRENT;
        values[D3DTSS_ALPHAOP] = D3DTOP_SELECTARG1;
        values[D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
        values[D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
        values[D3DTSS_COLORARG0] = D3DTA_CURRENT;
        values[D3DTSS_ALPHAARG0] = D3DTA_CURRENT;
        values[D3DTSS_TEXCOORDINDEX] = 0;
        values[D3DTSS_ADDRESSU] = D3DTADDRESS_WRAP;
        values[D3DTSS_ADDRESSV] = D3DTADDRESS_WRAP;
        values[D3DTSS_MAGFILTER] = D3DTEXF_POINT;
        values[D3DTSS_MINFILTER] = D3DTEXF_POINT;
        values[D3DTSS_MIPFILTER] = D3DTEXF_NONE;
    }
};

struct LightState {
    D3DLIGHT8 light{};
    bool enabled = false;
    bool defined = false;
};

// Opaque backend resource handle. 0 = null.
using BackendHandle = uint32_t;

struct StateSnapshot {
    // Render states, indexed by D3DRENDERSTATETYPE, D3D8 defaults where they matter
    std::array<DWORD, D3DRS_MAX_SENTINEL> rs{};

    std::array<StageState, kMaxTextureStages> stages{};
    std::array<BackendHandle, kMaxTextureStages> textures{};   // bound texture per stage
    std::array<bool, kMaxTextureStages> textureHasAlpha{};

    D3DMATRIX world{}, view{}, projection{};
    std::array<D3DMATRIX, kMaxTextureStages> texMatrices{};   // D3DTS_TEXTURE0..7
    D3DVIEWPORT8 viewport{};
    D3DMATERIAL8 material{};
    std::array<LightState, kMaxLights> lights{};

    DWORD fvf = 0;

    StateSnapshot() {
        auto identity = [](D3DMATRIX& m) {
            m = {};
            m._11 = m._22 = m._33 = m._44 = 1.0f;
        };
        identity(world); identity(view); identity(projection);
        for (auto& m : texMatrices) identity(m);

        // Stages >= 1 default to DISABLE (StageState's ctor holds stage-0 defaults)
        for (int i = 1; i < kMaxTextureStages; ++i) {
            stages[i].values[D3DTSS_COLOROP] = D3DTOP_DISABLE;
            stages[i].values[D3DTSS_ALPHAOP] = D3DTOP_DISABLE;
        }

        rs[D3DRS_ZENABLE] = TRUE;
        rs[D3DRS_ZWRITEENABLE] = TRUE;
        rs[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
        rs[D3DRS_CULLMODE] = D3DCULL_CCW;
        rs[D3DRS_FILLMODE] = D3DFILL_SOLID;
        rs[D3DRS_SHADEMODE] = D3DSHADE_GOURAUD;
        rs[D3DRS_SRCBLEND] = D3DBLEND_ONE;
        rs[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
        rs[D3DRS_ALPHABLENDENABLE] = FALSE;
        rs[D3DRS_ALPHATESTENABLE] = FALSE;
        rs[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
        rs[D3DRS_LIGHTING] = TRUE;
        rs[D3DRS_COLORVERTEX] = TRUE;
        rs[D3DRS_AMBIENT] = 0;
        rs[D3DRS_FOGENABLE] = FALSE;
        rs[D3DRS_FOGTABLEMODE] = D3DFOG_NONE;
        rs[D3DRS_FOGVERTEXMODE] = D3DFOG_NONE;
        rs[D3DRS_DIFFUSEMATERIALSOURCE] = D3DMCS_COLOR1;
        rs[D3DRS_SPECULARMATERIALSOURCE] = D3DMCS_COLOR2;
        rs[D3DRS_AMBIENTMATERIALSOURCE] = D3DMCS_MATERIAL;
        rs[D3DRS_EMISSIVEMATERIALSOURCE] = D3DMCS_MATERIAL;
        rs[D3DRS_COLORWRITEENABLE] = 0xF;
        rs[D3DRS_STENCILENABLE] = FALSE;
        rs[D3DRS_ALPHAREF] = 0;
    }
};

// Vertex layout decoded from an FVF code
struct VertexLayout {
    bool xyz = false;
    bool xyzrhw = false;      // pre-transformed (UI path)
    bool normal = false;
    bool diffuse = false;
    bool specular = false;
    int texCoordSets = 0;
    UINT stride = 0;

    UINT offsetPosition = 0;
    UINT offsetNormal = 0;
    UINT offsetDiffuse = 0;
    UINT offsetSpecular = 0;
    UINT offsetTex0 = 0;

    static VertexLayout fromFVF(DWORD fvf) {
        VertexLayout l;
        UINT off = 0;
        if (fvf & D3DFVF_XYZRHW) { l.xyzrhw = true; l.offsetPosition = off; off += 16; }
        else if (fvf & D3DFVF_XYZ) { l.xyz = true; l.offsetPosition = off; off += 12; }
        if (fvf & D3DFVF_NORMAL) { l.normal = true; l.offsetNormal = off; off += 12; }
        if (fvf & D3DFVF_DIFFUSE) { l.diffuse = true; l.offsetDiffuse = off; off += 4; }
        if (fvf & D3DFVF_SPECULAR) { l.specular = true; l.offsetSpecular = off; off += 4; }
        l.texCoordSets = int((fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT);
        if (l.texCoordSets > 0) l.offsetTex0 = off;
        off += UINT(l.texCoordSets) * 8;  // 2D texcoords only for now
        l.stride = off;
        return l;
    }
};

}  // namespace d8web
