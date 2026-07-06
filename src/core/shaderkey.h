// d8web core — ShaderKey: the FFP state bits that change generated shader code.
// Backend-agnostic: the GLSL emitter (WebGL2) and a future WGSL emitter (WebGPU)
// both consume this. Anything expressible as a uniform (fog params, alpha ref,
// light values, matrices) does NOT belong in the key — only code-shape changes do.
#pragma once

#include "state.h"

#include <cstdint>
#include <functional>

namespace d8web {

struct StageKey {
    uint8_t colorOp = D3DTOP_DISABLE;   // D3DTEXTUREOP
    uint8_t colorArg1 = D3DTA_TEXTURE;
    uint8_t colorArg2 = D3DTA_CURRENT;
    uint8_t colorArg0 = D3DTA_CURRENT;  // third arg (MULTIPLYADD/LERP)
    uint8_t alphaOp = D3DTOP_DISABLE;
    uint8_t alphaArg1 = D3DTA_TEXTURE;
    uint8_t alphaArg2 = D3DTA_CURRENT;
    uint8_t alphaArg0 = D3DTA_CURRENT;
    uint8_t texCoordIndex = 0;
    uint8_t texGen = 0;                  // D3DTSS_TCI_* >> 16 (0 = passthru)
    uint8_t texXform = 0;                // D3DTTFF count (0 = disabled)
    bool texProjected = false;           // D3DTTFF_PROJECTED
    bool bound = false;                  // texture bound on this stage

    bool operator==(const StageKey&) const = default;
};

struct ShaderKey {
    // Vertex input shape
    bool hasNormal = false;
    bool hasDiffuse = false;
    bool preTransformed = false;   // XYZRHW path (UI): skip WVP, positions in screen space
    uint8_t texCoordSets = 0;

    // Lighting (code shape: which light loops get emitted)
    bool lighting = false;
    uint8_t directionalLights = 0;
    uint8_t pointLights = 0;

    bool fogLinear = false;
    bool alphaTest = false;
    uint8_t alphaFunc = D3DCMP_ALWAYS;

    std::array<StageKey, kActiveStages> stages{};

    bool operator==(const ShaderKey&) const = default;

    // Build from the current snapshot + vertex layout
    static ShaderKey from(const StateSnapshot& s, const VertexLayout& layout) {
        ShaderKey k;
        k.hasNormal = layout.normal;
        k.hasDiffuse = layout.diffuse;
        k.preTransformed = layout.xyzrhw;
        k.texCoordSets = uint8_t(layout.texCoordSets);

        k.lighting = s.rs[D3DRS_LIGHTING] && layout.normal && !layout.xyzrhw;
        if (k.lighting) {
            for (const auto& l : s.lights) {
                if (!l.enabled || !l.defined) continue;
                // Clamp to the GLSL uniform array size (4) — more enabled lights
                // would emit out-of-bounds uDirLights[i]/uPointLights[i]
                if (l.light.Type == D3DLIGHT_DIRECTIONAL && k.directionalLights < 4)
                    k.directionalLights++;
                else if (l.light.Type == D3DLIGHT_POINT && k.pointLights < 4)
                    k.pointLights++;
            }
        }

        k.fogLinear = s.rs[D3DRS_FOGENABLE] &&
                      (s.rs[D3DRS_FOGTABLEMODE] == D3DFOG_LINEAR ||
                       s.rs[D3DRS_FOGVERTEXMODE] == D3DFOG_LINEAR);

        k.alphaTest = s.rs[D3DRS_ALPHATESTENABLE] != 0;
        k.alphaFunc = uint8_t(s.rs[D3DRS_ALPHAFUNC]);

        for (int i = 0; i < kActiveStages; ++i) {
            const auto& st = s.stages[i];
            StageKey& sk = k.stages[i];
            sk.colorOp = uint8_t(st.values[D3DTSS_COLOROP]);
            sk.colorArg1 = uint8_t(st.values[D3DTSS_COLORARG1] & 0xFF);
            sk.colorArg2 = uint8_t(st.values[D3DTSS_COLORARG2] & 0xFF);
            sk.colorArg0 = uint8_t(st.values[D3DTSS_COLORARG0] & 0xFF);
            sk.alphaOp = uint8_t(st.values[D3DTSS_ALPHAOP]);
            sk.alphaArg1 = uint8_t(st.values[D3DTSS_ALPHAARG1] & 0xFF);
            sk.alphaArg2 = uint8_t(st.values[D3DTSS_ALPHAARG2] & 0xFF);
            sk.alphaArg0 = uint8_t(st.values[D3DTSS_ALPHAARG0] & 0xFF);
            sk.texCoordIndex = uint8_t(st.values[D3DTSS_TEXCOORDINDEX] & 0xFF);
            // Texgen + texture-coordinate transform. The pre-transformed UI path
            // never uses either — zero them there so UI keys stay stable.
            if (!layout.xyzrhw) {
                sk.texGen = uint8_t((st.values[D3DTSS_TEXCOORDINDEX] >> 16) & 0xF);
                DWORD ttff = st.values[D3DTSS_TEXTURETRANSFORMFLAGS];
                sk.texXform = uint8_t(ttff & 0xFF);
                sk.texProjected = (ttff & D3DTTFF_PROJECTED) != 0;
            }
            sk.bound = s.textures[i] != 0;
            // No texture bound but ops reference D3DTA_TEXTURE: D3D8 degrades the
            // stage to a passthrough of the current color, not "texture = white".
            if (!sk.bound && sk.colorOp != D3DTOP_DISABLE) {
                auto refsTexture = [](uint8_t arg) { return (arg & D3DTA_SELECTMASK) == D3DTA_TEXTURE; };
                if (refsTexture(sk.colorArg1) || refsTexture(sk.colorArg2)) {
                    sk.colorOp = D3DTOP_SELECTARG2;
                    sk.colorArg1 = D3DTA_TEXTURE;
                    sk.colorArg2 = D3DTA_CURRENT;
                }
                if (sk.alphaOp != D3DTOP_DISABLE &&
                    (refsTexture(sk.alphaArg1) || refsTexture(sk.alphaArg2))) {
                    sk.alphaOp = D3DTOP_SELECTARG2;
                    sk.alphaArg1 = D3DTA_TEXTURE;
                    sk.alphaArg2 = D3DTA_CURRENT;
                }
            }
        }
        return k;
    }

    size_t hash() const {
        // FNV-1a over the raw bytes; key is trivially copyable and padding-free enough
        // for our purposes because we compare with == before trusting a bucket.
        const auto* p = reinterpret_cast<const unsigned char*>(this);
        uint64_t h = 1469598103934665603ull;
        for (size_t i = 0; i < sizeof(ShaderKey); ++i) {
            h ^= p[i];
            h *= 1099511628211ull;
        }
        // Fold to size_t (32-bit on wasm32) without truncation warnings
        return size_t(h ^ (h >> 32));
    }
};

struct ShaderKeyHash {
    size_t operator()(const ShaderKey& k) const { return k.hash(); }
};

}  // namespace d8web
