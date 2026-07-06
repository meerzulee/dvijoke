#include "glsl_emitter.h"

#include <sstream>

namespace d8web::webgl2 {
namespace {

// Uniform block shared by every generated program. One layout keeps the backend's
// upload code shader-independent; unused fields are cheap.
const char* kUniformBlock = R"(
uniform mat4 uWorld;
uniform mat4 uView;
uniform mat4 uProjection;
uniform vec4 uMaterialDiffuse;
uniform vec4 uMaterialAmbient;
uniform vec4 uMaterialEmissive;
uniform vec4 uGlobalAmbient;
uniform vec4 uFogColor;
uniform vec2 uFogRange;      // x = start, y = end (view-space distance)
uniform float uAlphaRef;
uniform vec4 uTextureFactor;
uniform vec2 uViewportSize;  // for XYZRHW screen->NDC
uniform mat4 uTexMat0;       // per-stage texture-coordinate transforms
uniform mat4 uTexMat1;
struct DirLight { vec3 dir; vec4 diffuse; vec4 ambient; };
uniform DirLight uDirLights[4];
struct PointLight { vec3 pos; vec4 diffuse; vec3 atten; float range; };
uniform PointLight uPointLights[4];
)";

// D3D texture-stage argument -> GLSL expression
std::string texArg(uint8_t arg, int stage, const ShaderKey& key) {
    switch (arg & D3DTA_SELECTMASK) {
        case D3DTA_DIFFUSE: return "vColor";
        case D3DTA_CURRENT: return "current";
        case D3DTA_TEXTURE: {
            if (!key.stages[stage].bound) return "vec4(1.0)";
            std::ostringstream s;
            s << "texture(uTex" << stage << ", vTexS" << stage << ")";
            return s.str();
        }
        case D3DTA_TFACTOR: return "uTextureFactor";
        default: return "vec4(1.0)";
    }
}

std::string applyModifiers(std::string expr, uint8_t arg) {
    if (arg & D3DTA_COMPLEMENT) expr = "(vec4(1.0) - " + expr + ")";
    if (arg & D3DTA_ALPHAREPLICATE) expr = "vec4(" + expr + ".a)";
    return expr;
}

// D3DTEXTUREOP -> GLSL combine expression over arg0/arg1/arg2 (vec4 or vec3
// slice applied by caller). arg0 is the third operand of MULTIPLYADD/LERP.
std::string combineOp(uint8_t op, const std::string& a1, const std::string& a2,
                      const std::string& a0) {
    switch (op) {
        case D3DTOP_MULTIPLYADD: return "(" + a0 + " + " + a1 + " * " + a2 + ")";
        case D3DTOP_LERP: return "mix(" + a2 + ", " + a1 + ", " + a0 + ")";
        case D3DTOP_SELECTARG1: return a1;
        case D3DTOP_SELECTARG2: return a2;
        case D3DTOP_MODULATE: return "(" + a1 + " * " + a2 + ")";
        case D3DTOP_MODULATE2X: return "(" + a1 + " * " + a2 + " * 2.0)";
        case D3DTOP_MODULATE4X: return "(" + a1 + " * " + a2 + " * 4.0)";
        case D3DTOP_ADD: return "(" + a1 + " + " + a2 + ")";
        case D3DTOP_ADDSIGNED: return "(" + a1 + " + " + a2 + " - 0.5)";
        case D3DTOP_SUBTRACT: return "(" + a1 + " - " + a2 + ")";
        case D3DTOP_BLENDTEXTUREALPHA:
            return "mix(" + a2 + ", " + a1 + ", " + a1 + ".a)";
        case D3DTOP_BLENDDIFFUSEALPHA:
            return "mix(" + a2 + ", " + a1 + ", vColor.a)";
        case D3DTOP_BLENDCURRENTALPHA:
            return "mix(" + a2 + ", " + a1 + ", current.a)";
        case D3DTOP_DOTPRODUCT3:
            return "vec4(vec3(4.0 * dot(" + a1 + ".rgb - 0.5, " + a2 + ".rgb - 0.5)), 1.0)";
        default: return a1;
    }
}

const char* alphaTestCmp(uint8_t func) {
    // Test is "keep if (alpha CMP ref)"; we emit the discard condition (negated).
    // D3D compares 8-bit quantized alpha — aQ/refQ are pre-quantized in the shader,
    // which makes EQUAL/NOTEQUAL meaningful instead of float-exact comparisons.
    switch (func) {
        case D3DCMP_NEVER: return "true";
        case D3DCMP_LESS: return "aQ >= refQ";
        case D3DCMP_EQUAL: return "aQ != refQ";
        case D3DCMP_LESSEQUAL: return "aQ > refQ";
        case D3DCMP_GREATER: return "aQ <= refQ";
        case D3DCMP_NOTEQUAL: return "aQ == refQ";
        case D3DCMP_GREATEREQUAL: return "aQ < refQ";
        default: return "false";  // ALWAYS: never discard
    }
}

}  // namespace

ShaderSource emitGLSL(const ShaderKey& key) {
    std::ostringstream vs, fs;

    // ---------------- Vertex shader ----------------
    vs << "#version 300 es\nprecision highp float;\n" << kUniformBlock;
    vs << "layout(location=0) in " << (key.preTransformed ? "vec4" : "vec3") << " aPosition;\n";
    if (key.hasNormal) vs << "layout(location=1) in vec3 aNormal;\n";
    if (key.hasDiffuse) vs << "layout(location=2) in vec4 aColor;\n";
    for (int i = 0; i < key.texCoordSets; ++i)
        vs << "layout(location=" << (3 + i) << ") in vec2 aTex" << i << ";\n";

    int liveStages = 0;
    while (liveStages < kActiveStages && key.stages[liveStages].colorOp != D3DTOP_DISABLE)
        ++liveStages;

    vs << "out vec4 vColor;\n";
    for (int i = 0; i < liveStages; ++i) vs << "out vec2 vTexS" << i << ";\n";
    if (key.fogLinear) vs << "out float vFogDist;\n";

    vs << "void main() {\n";
    if (key.preTransformed) {
        // XYZRHW: x,y in pixels, z in [0,1], w = 1/w. Map to NDC; flip Y.
        vs << "  vec2 ndc = vec2(aPosition.x / uViewportSize.x * 2.0 - 1.0,\n"
              "                  1.0 - aPosition.y / uViewportSize.y * 2.0);\n"
              "  gl_Position = vec4(ndc, aPosition.z * 2.0 - 1.0, 1.0);\n";
    } else {
        vs << "  vec4 worldPos = uWorld * vec4(aPosition, 1.0);\n"
              "  vec4 viewPos = uView * worldPos;\n"
              "  gl_Position = uProjection * viewPos;\n"
              // D3D8 rasterizes with pixel centers at integer coordinates; GL
              // uses half-integers. 1:1 texel-mapped 2D UI (menu buttons tiled
              // from a texture segment) shows a seam at every segment unless we
              // shift clip space half a pixel. Apply ONLY for orthographic
              // draws — a perspective projection has m[3][3]==0, and shifting
              // the 3D world misaligns the water plane against terrain (visible
              // as a sawtooth gap at the shoreline).
              "  if (uProjection[3][3] > 0.5)\n"
              "    gl_Position.xy += vec2(-1.0, 1.0) / uViewportSize * gl_Position.w;\n";
        // D3D LH view space: camera looks down +Z, visible depth is positive
        if (key.fogLinear) vs << "  vFogDist = viewPos.z;\n";
    }

    if (key.lighting) {
        vs << "  vec3 N = normalize(mat3(uWorld) * aNormal);\n";
        // Material color sources: COLORVERTEX + COLOR1 handled as: diffuse from
        // vertex color when present, else material. (Full MCS matrix later.)
        // D3DCOLOR vertex colors are BGRA in memory; GL reads bytes as RGBA — swizzle here
        std::string matDiffuse = key.hasDiffuse ? "aColor.bgra" : "uMaterialDiffuse";
        vs << "  vec4 diffuseAccum = vec4(0.0);\n"
              "  vec4 ambientAccum = uGlobalAmbient * uMaterialAmbient;\n";
        for (int i = 0; i < key.directionalLights; ++i) {
            vs << "  {\n"
                  "    float ndl = max(dot(N, -normalize(uDirLights[" << i << "].dir)), 0.0);\n"
                  "    diffuseAccum += uDirLights[" << i << "].diffuse * ndl;\n"
                  "    ambientAccum += uDirLights[" << i << "].ambient * uMaterialAmbient;\n"
                  "  }\n";
        }
        for (int i = 0; i < key.pointLights; ++i) {
            vs << "  {\n"
                  "    vec3 toL = uPointLights[" << i << "].pos - worldPos.xyz;\n"
                  "    float dist = length(toL);\n"
                  "    if (dist < uPointLights[" << i << "].range) {\n"
                  "      float ndl = max(dot(N, toL / dist), 0.0);\n"
                  "      float att = 1.0 / (uPointLights[" << i << "].atten.x +\n"
                  "                         uPointLights[" << i << "].atten.y * dist +\n"
                  "                         uPointLights[" << i << "].atten.z * dist * dist);\n"
                  "      diffuseAccum += uPointLights[" << i << "].diffuse * ndl * att;\n"
                  "    }\n"
                  "  }\n";
        }
        vs << "  vColor = clamp(ambientAccum + diffuseAccum * " << matDiffuse
           << " + uMaterialEmissive, 0.0, 1.0);\n"
           << "  vColor.a = " << matDiffuse << ".a;\n";
    } else {
        vs << "  vColor = " << (key.hasDiffuse ? "aColor.bgra" : "vec4(1.0)") << ";\n";
    }

    // Per-stage texture coordinates: vertex UVs, or generated (texgen), then
    // optionally run through the stage's texture matrix (D3D convention: input
    // padded with a trailing 1, row-vector times matrix — our column-major
    // upload of the row-major D3D matrix makes uTexMat * v equivalent).
    for (int i = 0; i < liveStages; ++i) {
        const StageKey& st = key.stages[i];
        std::string mat = (i == 0) ? "uTexMat0" : "uTexMat1";
        auto uvExpr = [&]() -> std::string {
            int coord = st.texCoordIndex;
            if (coord >= key.texCoordSets) coord = 0;
            if (key.texCoordSets == 0) return "vec2(0.0)";
            return "aTex" + std::to_string(coord);
        };
        vs << "  {\n";
        if (st.texGen == 0) {
            if (st.texXform) {
                vs << "    vec4 tc = " << mat << " * vec4(" << uvExpr() << ", 1.0, 0.0);\n";
                if (st.texProjected && st.texXform >= 3)
                    vs << "    vTexS" << i << " = tc.xy / (abs(tc.z) < 1e-6 ? 1e-6 : tc.z);\n";
                else
                    vs << "    vTexS" << i << " = tc.xy;\n";
            } else {
                vs << "    vTexS" << i << " = " << uvExpr() << ";\n";
            }
        } else {
            // Camera-space texgen sources
            std::string n = key.hasNormal
                ? "normalize(mat3(uView) * (mat3(uWorld) * aNormal))"
                : "vec3(0.0, 0.0, 1.0)";
            vs << "    vec3 src;\n";
            switch (st.texGen) {
                case 1:  // D3DTSS_TCI_CAMERASPACENORMAL >> 16
                    vs << "    src = " << n << ";\n"; break;
                case 2:  // CAMERASPACEPOSITION
                    vs << "    src = viewPos.xyz;\n"; break;
                default: // CAMERASPACEREFLECTIONVECTOR
                    vs << "    src = reflect(normalize(viewPos.xyz), " << n << ");\n"; break;
            }
            if (st.texXform) {
                vs << "    vec4 tc = " << mat << " * vec4(src, 1.0);\n";
                if (st.texProjected && st.texXform >= 3)
                    vs << "    vTexS" << i << " = tc.xy / (abs(tc.z) < 1e-6 ? 1e-6 : tc.z);\n";
                else
                    vs << "    vTexS" << i << " = tc.xy;\n";
            } else {
                vs << "    vTexS" << i << " = src.xy;\n";
            }
        }
        vs << "  }\n";
    }
    vs << "}\n";

    // ---------------- Fragment shader ----------------
    // highp: texture coordinates derived from camera-space positions (cloud/
    // shroud texgen) carry large magnitudes; mediump interpolation quantizes
    // their fractional part and the sampled texel drifts (codex finding).
    fs << "#version 300 es\nprecision highp float;\n" << kUniformBlock;
    fs << "in vec4 vColor;\n";
    for (int i = 0; i < liveStages; ++i) fs << "in vec2 vTexS" << i << ";\n";
    if (key.fogLinear) fs << "in float vFogDist;\n";
    for (int i = 0; i < kActiveStages; ++i)
        if (key.stages[i].bound) fs << "uniform sampler2D uTex" << i << ";\n";
    fs << "out vec4 outColor;\n";

    fs << "void main() {\n  vec4 current = vColor;\n";
    for (int i = 0; i < kActiveStages; ++i) {
        const StageKey& st = key.stages[i];
        if (st.colorOp == D3DTOP_DISABLE) break;  // D3D8: disable terminates the chain
        std::string c1 = applyModifiers(texArg(st.colorArg1, i, key), st.colorArg1);
        std::string c2 = applyModifiers(texArg(st.colorArg2, i, key), st.colorArg2);
        std::string c0 = applyModifiers(texArg(st.colorArg0, i, key), st.colorArg0);
        fs << "  {\n    vec4 stageOut;\n"
           << "    stageOut.rgb = " << combineOp(st.colorOp, c1, c2, c0) << ".rgb;\n";
        if (st.alphaOp == D3DTOP_DISABLE) {
            fs << "    stageOut.a = current.a;\n";
        } else {
            std::string a1 = applyModifiers(texArg(st.alphaArg1, i, key), st.alphaArg1);
            std::string a2 = applyModifiers(texArg(st.alphaArg2, i, key), st.alphaArg2);
            std::string a0 = applyModifiers(texArg(st.alphaArg0, i, key), st.alphaArg0);
            fs << "    stageOut.a = " << combineOp(st.alphaOp, a1, a2, a0) << ".a;\n";
        }
        fs << "    current = clamp(stageOut, 0.0, 1.0);\n  }\n";
    }
    fs << "  outColor = current;\n";

    if (key.fogLinear) {
        fs << "  float fogF = clamp((uFogRange.y - vFogDist) / (uFogRange.y - uFogRange.x), 0.0, 1.0);\n"
              "  outColor.rgb = mix(uFogColor.rgb, outColor.rgb, fogF);\n";
    }
    if (key.alphaTest) {
        fs << "  int aQ = int(outColor.a * 255.0 + 0.5);\n"
              "  int refQ = int(uAlphaRef * 255.0 + 0.5);\n"
              "  if (" << alphaTestCmp(key.alphaFunc) << ") discard;\n";
    }
    fs << "}\n";

    return {vs.str(), fs.str()};
}

}  // namespace d8web::webgl2
