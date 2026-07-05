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
            int coord = key.stages[stage].texCoordIndex;
            if (coord >= key.texCoordSets) coord = 0;
            s << "texture(uTex" << stage << ", vTex" << coord << ")";
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

// D3DTEXTUREOP -> GLSL combine expression over arg1/arg2 (vec4 or vec3 slice applied by caller)
std::string combineOp(uint8_t op, const std::string& a1, const std::string& a2) {
    switch (op) {
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

    vs << "out vec4 vColor;\n";
    for (int i = 0; i < key.texCoordSets; ++i) vs << "out vec2 vTex" << i << ";\n";
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
              "  gl_Position = uProjection * viewPos;\n";
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

    for (int i = 0; i < key.texCoordSets; ++i) vs << "  vTex" << i << " = aTex" << i << ";\n";
    vs << "}\n";

    // ---------------- Fragment shader ----------------
    fs << "#version 300 es\nprecision mediump float;\n" << kUniformBlock;
    fs << "in vec4 vColor;\n";
    for (int i = 0; i < key.texCoordSets; ++i) fs << "in vec2 vTex" << i << ";\n";
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
        fs << "  {\n    vec4 stageOut;\n"
           << "    stageOut.rgb = " << combineOp(st.colorOp, c1, c2) << ".rgb;\n";
        if (st.alphaOp == D3DTOP_DISABLE) {
            fs << "    stageOut.a = current.a;\n";
        } else {
            std::string a1 = applyModifiers(texArg(st.alphaArg1, i, key), st.alphaArg1);
            std::string a2 = applyModifiers(texArg(st.alphaArg2, i, key), st.alphaArg2);
            fs << "    stageOut.a = " << combineOp(st.alphaOp, a1, a2) << ".a;\n";
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
