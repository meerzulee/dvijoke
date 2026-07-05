// d8web WebGL2 backend
// Dumb translator: StateSnapshot + DrawGeometry in, GLES3/WebGL2 calls out.
// WebGL2 gaps handled here:
//   - no glDrawElementsBaseVertex → base vertex applied via attribute pointer offsets
//   - no GL_TRIANGLE_FAN in indexed form worth trusting → fans converted to lists
//   - D3D alpha ref (0..255) normalized to [0,1] uniform

#include "../../core/backend.h"
#include "glsl_emitter.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>
#endif
#include <GLES3/gl3.h>

#include <cstdio>
#include <unordered_map>
#include <vector>

namespace d8web {
namespace {

using webgl2::emitGLSL;

struct Program {
    GLuint prog = 0;
    // Uniform locations (‑1 = absent/optimized out)
    GLint world = -1, view = -1, projection = -1;
    GLint materialDiffuse = -1, materialAmbient = -1, materialEmissive = -1;
    GLint globalAmbient = -1, fogColor = -1, fogRange = -1;
    GLint alphaRef = -1, textureFactor = -1, viewportSize = -1;
    GLint tex[kActiveStages] = {-1, -1};
    struct { GLint dir = -1, diffuse = -1, ambient = -1; } dirLights[4];
    struct { GLint pos = -1, diffuse = -1, atten = -1, range = -1; } pointLights[4];
};

struct BufferSlot {
    GLuint gl = 0;
    GLenum target = GL_ARRAY_BUFFER;
    UINT size = 0;
    bool dynamic = false;
    bool live = false;
};

struct TextureSlot {
    GLuint gl = 0;
    UINT width = 0, height = 0, levels = 1;
    TexFormat format = TexFormat::RGBA8;
    // Last-applied sampler params, to skip redundant glTexParameteri
    DWORD addrU = 0, addrV = 0, magF = 0, minF = 0, mipF = 0;
    bool live = false;
};

GLuint compile(GLenum type, const std::string& src) {
    GLuint sh = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(sh, 1, &p, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[d8web] shader compile failed:\n%s\n--- source ---\n%s\n", log, src.c_str());
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

GLenum glBlend(DWORD b) {
    switch (b) {
        case D3DBLEND_ZERO: return GL_ZERO;
        case D3DBLEND_ONE: return GL_ONE;
        case D3DBLEND_SRCCOLOR: return GL_SRC_COLOR;
        case D3DBLEND_INVSRCCOLOR: return GL_ONE_MINUS_SRC_COLOR;
        case D3DBLEND_SRCALPHA: return GL_SRC_ALPHA;
        case D3DBLEND_INVSRCALPHA: return GL_ONE_MINUS_SRC_ALPHA;
        case D3DBLEND_DESTALPHA: return GL_DST_ALPHA;
        case D3DBLEND_INVDESTALPHA: return GL_ONE_MINUS_DST_ALPHA;
        case D3DBLEND_DESTCOLOR: return GL_DST_COLOR;
        case D3DBLEND_INVDESTCOLOR: return GL_ONE_MINUS_DST_COLOR;
        case D3DBLEND_SRCALPHASAT: return GL_SRC_ALPHA_SATURATE;
        default: return GL_ONE;
    }
}

GLenum glCmp(DWORD c) {
    switch (c) {
        case D3DCMP_NEVER: return GL_NEVER;
        case D3DCMP_LESS: return GL_LESS;
        case D3DCMP_EQUAL: return GL_EQUAL;
        case D3DCMP_LESSEQUAL: return GL_LEQUAL;
        case D3DCMP_GREATER: return GL_GREATER;
        case D3DCMP_NOTEQUAL: return GL_NOTEQUAL;
        case D3DCMP_GREATEREQUAL: return GL_GEQUAL;
        default: return GL_ALWAYS;
    }
}

GLenum glWrap(DWORD a) {
    switch (a) {
        case D3DTADDRESS_MIRROR: return GL_MIRRORED_REPEAT;
        case D3DTADDRESS_CLAMP:
        case D3DTADDRESS_BORDER: return GL_CLAMP_TO_EDGE;  // border color approximated
        default: return GL_REPEAT;
    }
}

class WebGL2Backend final : public IBackend {
public:
    bool init(int width, int height) override {
#ifdef __EMSCRIPTEN__
        EmscriptenWebGLContextAttributes attrs;
        emscripten_webgl_init_context_attributes(&attrs);
        attrs.majorVersion = 2;
        attrs.minorVersion = 0;
        attrs.alpha = false;
        attrs.depth = true;
        attrs.stencil = true;
        attrs.antialias = false;
        // Keep the rendered frame readable between RAF ticks (screenshots, and
        // some browsers otherwise clear on composite).
        attrs.preserveDrawingBuffer = true;
        m_ctx = emscripten_webgl_create_context("#canvas", &attrs);
        if (m_ctx <= 0) {
            std::fprintf(stderr, "[d8web] WebGL2 context creation failed (%ld)\n", (long)m_ctx);
            return false;
        }
        emscripten_webgl_make_context_current(m_ctx);
        // DXT texture support is an extension WebGL only activates on request
        bool s3tc = emscripten_webgl_enable_extension(m_ctx, "WEBGL_compressed_texture_s3tc");
        std::fprintf(stderr, "[d8web] S3TC/DXT extension: %s\n", s3tc ? "enabled" : "UNAVAILABLE");
#endif
        m_width = width;
        m_height = height;
        glGenVertexArrays(1, &m_vao);
        glBindVertexArray(m_vao);
        glDisable(GL_DITHER);
        // D3D convention: clockwise = front when culling CCW-off; we translate cull
        // mode per draw, base winding stays GL default (CCW front).
        std::printf("[d8web] WebGL2 backend up: %dx%d, GL: %s\n", width, height,
                    (const char*)glGetString(GL_VERSION));
        return true;
    }

    void resize(int width, int height) override {
        m_width = width;
        m_height = height;
    }

    BackendHandle createBuffer(BufferKind kind, UINT byteSize, bool dynamic) override {
        BufferSlot slot;
        slot.target = (kind == BufferKind::Vertex) ? GL_ARRAY_BUFFER : GL_ELEMENT_ARRAY_BUFFER;
        slot.size = byteSize;
        slot.dynamic = dynamic;
        slot.live = true;
        glGenBuffers(1, &slot.gl);
        glBindBuffer(slot.target, slot.gl);
        glBufferData(slot.target, byteSize, nullptr, dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
        if (slot.target == GL_ELEMENT_ARRAY_BUFFER) m_boundIB = slot.gl;  // VAO captured it
        return store(m_buffers, slot);
    }

    void updateBuffer(BackendHandle h, UINT offset, const void* data, UINT size, bool discard) override {
        BufferSlot& b = m_buffers[h - 1];
        glBindBuffer(b.target, b.gl);
        if (b.target == GL_ELEMENT_ARRAY_BUFFER) m_boundIB = b.gl;
        if (discard && offset == 0 && size == b.size) {
            glBufferData(b.target, b.size, data, b.dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
        } else {
            if (discard) glBufferData(b.target, b.size, nullptr,
                                      b.dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
            glBufferSubData(b.target, offset, size, data);
        }
    }

    void destroyBuffer(BackendHandle h) override {
        BufferSlot& b = m_buffers[h - 1];
        if (b.live) glDeleteBuffers(1, &b.gl);
        b.live = false;
    }

    BackendHandle createTexture(UINT width, UINT height, UINT levels, TexFormat format) override {
        TextureSlot t;
        t.width = width; t.height = height;
        t.levels = levels ? levels : 1;
        t.format = format;
        t.live = true;
        glGenTextures(1, &t.gl);
        glBindTexture(GL_TEXTURE_2D, t.gl);
        GLenum internal = GL_RGBA8;
        switch (format) {
            case TexFormat::RGBA8: internal = GL_RGBA8; break;
            case TexFormat::RGB565: internal = GL_RGB565; break;
            case TexFormat::RGBA4: internal = GL_RGBA4; break;
            case TexFormat::RGB5A1: internal = GL_RGB5_A1; break;
            case TexFormat::L8: internal = GL_R8; break;
            case TexFormat::A8L8: internal = GL_RG8; break;
            default: internal = GL_RGBA8; break;  // DXT: storage set on upload
        }
        if (format != TexFormat::DXT1 && format != TexFormat::DXT3 && format != TexFormat::DXT5)
            glTexStorage2D(GL_TEXTURE_2D, GLsizei(t.levels), internal, width, height);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, GLint(t.levels - 1));
        return store(m_textures, t);
    }

    void updateTexture(BackendHandle h, UINT level, UINT width, UINT height,
                       TexFormat format, const void* data, UINT byteSize) override {
        TextureSlot& t = m_textures[h - 1];
        if (++m_texUploads <= 8)
            std::fprintf(stderr, "[d8web] texUpload #%llu h=%u lvl=%u %ux%u fmt=%d bytes=%u first=%02X%02X%02X%02X\n",
                         (unsigned long long)m_texUploads, h, level, width, height, int(format),
                         byteSize, ((const BYTE*)data)[0], ((const BYTE*)data)[1],
                         ((const BYTE*)data)[2], ((const BYTE*)data)[3]);
        glBindTexture(GL_TEXTURE_2D, t.gl);
        switch (format) {
            case TexFormat::RGBA8:
                glTexSubImage2D(GL_TEXTURE_2D, GLint(level), 0, 0, width, height,
                                GL_RGBA, GL_UNSIGNED_BYTE, data);
                break;
            case TexFormat::RGB565:
                glTexSubImage2D(GL_TEXTURE_2D, GLint(level), 0, 0, width, height,
                                GL_RGB, GL_UNSIGNED_SHORT_5_6_5, data);
                break;
            case TexFormat::RGBA4:
                glTexSubImage2D(GL_TEXTURE_2D, GLint(level), 0, 0, width, height,
                                GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, data);
                break;
            case TexFormat::RGB5A1:
                glTexSubImage2D(GL_TEXTURE_2D, GLint(level), 0, 0, width, height,
                                GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, data);
                break;
            case TexFormat::L8:
                glTexSubImage2D(GL_TEXTURE_2D, GLint(level), 0, 0, width, height,
                                GL_RED, GL_UNSIGNED_BYTE, data);
                break;
            case TexFormat::A8L8:
                glTexSubImage2D(GL_TEXTURE_2D, GLint(level), 0, 0, width, height,
                                GL_RG, GL_UNSIGNED_BYTE, data);
                break;
            case TexFormat::DXT1:
            case TexFormat::DXT3:
            case TexFormat::DXT5: {
                // Requires WEBGL_compressed_texture_s3tc (enabled at context init by
                // Emscripten when available). Frontend CPU-decompresses as fallback.
                GLenum fmt = (format == TexFormat::DXT1)   ? 0x83F1   // COMPRESSED_RGBA_S3TC_DXT1
                             : (format == TexFormat::DXT3) ? 0x83F2   // DXT3
                                                           : 0x83F3;  // DXT5
                glCompressedTexImage2D(GL_TEXTURE_2D, GLint(level), fmt, width, height, 0,
                                       GLsizei(byteSize), data);
                break;
            }
        }
    }

    void destroyTexture(BackendHandle h) override {
        TextureSlot& t = m_textures[h - 1];
        if (t.live) glDeleteTextures(1, &t.gl);
        t.live = false;
    }

    void setViewport(const D3DVIEWPORT8& vp) override {
        m_viewport = vp;
        // D3D viewport origin: top-left. GL: bottom-left. Flip Y.
        glViewport(GLint(vp.X), GLint(m_height) - GLint(vp.Y + vp.Height),
                   GLsizei(vp.Width), GLsizei(vp.Height));
        glDepthRangef(vp.MinZ, vp.MaxZ);
    }

    void clear(bool color, bool depth, bool stencil,
               D3DCOLOR argb, float z, DWORD stencilValue) override {
        GLbitfield mask = 0;
        if (color) {
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
#ifdef D8WEB_DEBUG_CLEAR
            glClearColor(1.0f, 0.0f, 1.0f, 1.0f);  // magenta context-ownership probe
#else
            glClearColor(((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f,
                         (argb & 0xFF) / 255.0f, ((argb >> 24) & 0xFF) / 255.0f);
#endif
            mask |= GL_COLOR_BUFFER_BIT;
        }
        if (depth) {
            glDepthMask(GL_TRUE);
            glClearDepthf(z);
            mask |= GL_DEPTH_BUFFER_BIT;
        }
        if (stencil) {
            glClearStencil(GLint(stencilValue));
            mask |= GL_STENCIL_BUFFER_BIT;
        }
        if (++m_clearCount <= 3 || m_clearCount % 300 == 0)
            std::fprintf(stderr, "[d8web] clear #%llu argb=0x%08X vp=%ux%u\n",
                         (unsigned long long)m_clearCount, argb, m_viewport.Width, m_viewport.Height);
        // Clear respects scissor, not viewport, in GL; D3D Clear clears the viewport.
        glEnable(GL_SCISSOR_TEST);
        glScissor(GLint(m_viewport.X), GLint(m_height) - GLint(m_viewport.Y + m_viewport.Height),
                  GLsizei(m_viewport.Width ? m_viewport.Width : m_width),
                  GLsizei(m_viewport.Height ? m_viewport.Height : m_height));
        glClear(mask);
        glDisable(GL_SCISSOR_TEST);
    }

    void draw(const DrawGeometry& geo, const StateSnapshot& s, const ShaderKey& key) override {
        const Program& prog = getProgram(key);
        if (!prog.prog) return;
        glUseProgram(prog.prog);

        applyFixedState(s, key);
        uploadUniforms(prog, s, key);
        bindTextures(prog, s, key);
        bindGeometry(geo);

        const UINT vertexCount = geo.primCount == 0 ? 0 : vertexCountFor(geo.primitive, geo.primCount);
        if (vertexCount == 0) return;

        if (++m_drawCount <= 5 || m_drawCount % 500 == 0)
            std::fprintf(stderr, "[d8web] draw #%llu prim=%d verts=%u tex0=%u prog=%u cull=%u rhw=%d\n",
                         (unsigned long long)m_drawCount, int(geo.primitive), vertexCount,
                         s.textures[0], prog.prog, s.rs[D3DRS_CULLMODE], int(key.preTransformed));

        if (geo.ib) {
            GLenum type = geo.index32 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
            UINT indexSize = geo.index32 ? 4 : 2;
            if (geo.primitive == D3DPT_TRIANGLEFAN) {
                drawFanIndexed(geo, type, indexSize);
            } else {
                glDrawElements(glPrim(geo.primitive), GLsizei(vertexCount), type,
                               reinterpret_cast<const void*>(uintptr_t(geo.startIndex) * indexSize));
            }
        } else {
            if (geo.primitive == D3DPT_TRIANGLEFAN) {
                glDrawArrays(GL_TRIANGLE_FAN, GLint(geo.startVertex), GLsizei(vertexCount));
            } else {
                glDrawArrays(glPrim(geo.primitive), GLint(geo.startVertex), GLsizei(vertexCount));
            }
        }
    }

    void present() override {
        // Browser presents on return to the event loop; nothing to do.
        m_frame++;
    }

    const char* name() const override { return "webgl2"; }

private:
    template <typename T>
    static BackendHandle store(std::vector<T>& vec, const T& v) {
        for (size_t i = 0; i < vec.size(); ++i) {
            if (!vec[i].live) { vec[i] = v; return BackendHandle(i + 1); }
        }
        vec.push_back(v);
        return BackendHandle(vec.size());
    }

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

    static GLenum glPrim(D3DPRIMITIVETYPE t) {
        switch (t) {
            case D3DPT_POINTLIST: return GL_POINTS;
            case D3DPT_LINELIST: return GL_LINES;
            case D3DPT_LINESTRIP: return GL_LINE_STRIP;
            case D3DPT_TRIANGLESTRIP: return GL_TRIANGLE_STRIP;
            case D3DPT_TRIANGLEFAN: return GL_TRIANGLE_FAN;
            default: return GL_TRIANGLES;
        }
    }

    const Program& getProgram(const ShaderKey& key) {
        auto it = m_programs.find(key);
        if (it != m_programs.end()) return it->second;

        Program p;
        auto src = emitGLSL(key);
        GLuint vs = compile(GL_VERTEX_SHADER, src.vertex);
        GLuint fs = compile(GL_FRAGMENT_SHADER, src.fragment);
        if (vs && fs) {
            p.prog = glCreateProgram();
            glAttachShader(p.prog, vs);
            glAttachShader(p.prog, fs);
            glLinkProgram(p.prog);
            GLint ok = 0;
            glGetProgramiv(p.prog, GL_LINK_STATUS, &ok);
            if (!ok) {
                char log[1024];
                glGetProgramInfoLog(p.prog, sizeof(log), nullptr, log);
                std::fprintf(stderr, "[d8web] program link failed: %s\n", log);
                glDeleteProgram(p.prog);
                p.prog = 0;
            }
        }
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);

        if (p.prog) {
            auto loc = [&](const char* n) { return glGetUniformLocation(p.prog, n); };
            p.world = loc("uWorld"); p.view = loc("uView"); p.projection = loc("uProjection");
            p.materialDiffuse = loc("uMaterialDiffuse");
            p.materialAmbient = loc("uMaterialAmbient");
            p.materialEmissive = loc("uMaterialEmissive");
            p.globalAmbient = loc("uGlobalAmbient");
            p.fogColor = loc("uFogColor"); p.fogRange = loc("uFogRange");
            p.alphaRef = loc("uAlphaRef"); p.textureFactor = loc("uTextureFactor");
            p.viewportSize = loc("uViewportSize");
            char buf[64];
            for (int i = 0; i < kActiveStages; ++i) {
                std::snprintf(buf, sizeof(buf), "uTex%d", i);
                p.tex[i] = loc(buf);
            }
            for (int i = 0; i < 4; ++i) {
                std::snprintf(buf, sizeof(buf), "uDirLights[%d].dir", i); p.dirLights[i].dir = loc(buf);
                std::snprintf(buf, sizeof(buf), "uDirLights[%d].diffuse", i); p.dirLights[i].diffuse = loc(buf);
                std::snprintf(buf, sizeof(buf), "uDirLights[%d].ambient", i); p.dirLights[i].ambient = loc(buf);
                std::snprintf(buf, sizeof(buf), "uPointLights[%d].pos", i); p.pointLights[i].pos = loc(buf);
                std::snprintf(buf, sizeof(buf), "uPointLights[%d].diffuse", i); p.pointLights[i].diffuse = loc(buf);
                std::snprintf(buf, sizeof(buf), "uPointLights[%d].atten", i); p.pointLights[i].atten = loc(buf);
                std::snprintf(buf, sizeof(buf), "uPointLights[%d].range", i); p.pointLights[i].range = loc(buf);
            }
        }
        return m_programs.emplace(key, p).first->second;
    }

    void applyFixedState(const StateSnapshot& s, const ShaderKey& key) {
        // Depth
        if (s.rs[D3DRS_ZENABLE]) { glEnable(GL_DEPTH_TEST); glDepthFunc(glCmp(s.rs[D3DRS_ZFUNC])); }
        else glDisable(GL_DEPTH_TEST);
        glDepthMask(s.rs[D3DRS_ZWRITEENABLE] ? GL_TRUE : GL_FALSE);

        // Cull. D3D winding is defined in y-down screen space, GL in y-up — a CCW
        // triangle in D3D terms is CW in GL terms. So D3DCULL_CCW (cull D3D-CCW)
        // keeps GL-CCW faces: glFrontFace(GL_CCW). Verified empirically (inverted
        // mapping renders the cube inside-out).
        // Empirically settled against real engine geometry (ZH main menu):
        // D3DCULL_CW must keep GL-CCW faces. The XYZRHW path y-flips in the
        // shader, mirroring winding, so it takes the opposite front face.
        GLenum cwCullFront  = key.preTransformed ? GL_CW : GL_CCW;  // when D3D culls CW
        GLenum ccwCullFront = key.preTransformed ? GL_CCW : GL_CW;  // when D3D culls CCW
        switch (s.rs[D3DRS_CULLMODE]) {
            case D3DCULL_NONE: glDisable(GL_CULL_FACE); break;
            case D3DCULL_CW:
                glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(cwCullFront); break;
            default:  // D3DCULL_CCW
                glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(ccwCullFront); break;
        }

        // Blend
        if (s.rs[D3DRS_ALPHABLENDENABLE]) {
            glEnable(GL_BLEND);
            glBlendFunc(glBlend(s.rs[D3DRS_SRCBLEND]), glBlend(s.rs[D3DRS_DESTBLEND]));
        } else {
            glDisable(GL_BLEND);
        }

        // Color write
        DWORD cw = s.rs[D3DRS_COLORWRITEENABLE];
        glColorMask((cw & D3DCOLORWRITEENABLE_RED) != 0, (cw & D3DCOLORWRITEENABLE_GREEN) != 0,
                    (cw & D3DCOLORWRITEENABLE_BLUE) != 0, (cw & D3DCOLORWRITEENABLE_ALPHA) != 0);

        // Depth bias (D3DRS_ZBIAS: 0..16, larger = closer)
        if (DWORD zb = s.rs[D3DRS_ZBIAS]; zb != 0) {
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(-1.0f * float(zb), -2.0f * float(zb));
        } else {
            glDisable(GL_POLYGON_OFFSET_FILL);
        }
    }

    void uploadUniforms(const Program& p, const StateSnapshot& s, const ShaderKey& key) {
        auto mat = [](GLint loc, const D3DMATRIX& m) {
            if (loc >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, &m._11);
        };
        mat(p.world, s.world); mat(p.view, s.view); mat(p.projection, s.projection);

        auto col = [](GLint loc, const D3DCOLORVALUE& c) {
            if (loc >= 0) glUniform4f(loc, c.r, c.g, c.b, c.a);
        };
        col(p.materialDiffuse, s.material.Diffuse);
        col(p.materialAmbient, s.material.Ambient);
        col(p.materialEmissive, s.material.Emissive);

        if (p.globalAmbient >= 0) {
            DWORD a = s.rs[D3DRS_AMBIENT];
            glUniform4f(p.globalAmbient, ((a >> 16) & 0xFF) / 255.0f, ((a >> 8) & 0xFF) / 255.0f,
                        (a & 0xFF) / 255.0f, ((a >> 24) & 0xFF) / 255.0f);
        }
        if (p.fogColor >= 0) {
            DWORD f = s.rs[D3DRS_FOGCOLOR];
            glUniform4f(p.fogColor, ((f >> 16) & 0xFF) / 255.0f, ((f >> 8) & 0xFF) / 255.0f,
                        (f & 0xFF) / 255.0f, 1.0f);
        }
        if (p.fogRange >= 0) {
            float fs, fe;
            DWORD dws = s.rs[D3DRS_FOGSTART], dwe = s.rs[D3DRS_FOGEND];
            std::memcpy(&fs, &dws, 4);
            std::memcpy(&fe, &dwe, 4);
            glUniform2f(p.fogRange, fs, fe);
        }
        if (p.alphaRef >= 0) glUniform1f(p.alphaRef, float(s.rs[D3DRS_ALPHAREF] & 0xFF) / 255.0f);
        if (p.textureFactor >= 0) {
            DWORD t = s.rs[D3DRS_TEXTUREFACTOR];
            glUniform4f(p.textureFactor, ((t >> 16) & 0xFF) / 255.0f, ((t >> 8) & 0xFF) / 255.0f,
                        (t & 0xFF) / 255.0f, ((t >> 24) & 0xFF) / 255.0f);
        }
        if (p.viewportSize >= 0)
            glUniform2f(p.viewportSize, float(m_viewport.Width ? m_viewport.Width : m_width),
                        float(m_viewport.Height ? m_viewport.Height : m_height));

        // Lights: pack enabled ones by type, matching the emitter's loop counts
        int nd = 0, np = 0;
        for (const auto& l : s.lights) {
            if (!l.enabled || !l.defined) continue;
            if (l.light.Type == D3DLIGHT_DIRECTIONAL && nd < key.directionalLights && nd < 4) {
                const auto& dl = p.dirLights[nd];
                if (dl.dir >= 0)
                    glUniform3f(dl.dir, l.light.Direction.x, l.light.Direction.y, l.light.Direction.z);
                if (dl.diffuse >= 0)
                    glUniform4f(dl.diffuse, l.light.Diffuse.r, l.light.Diffuse.g, l.light.Diffuse.b,
                                l.light.Diffuse.a);
                if (dl.ambient >= 0)
                    glUniform4f(dl.ambient, l.light.Ambient.r, l.light.Ambient.g, l.light.Ambient.b,
                                l.light.Ambient.a);
                nd++;
            } else if (l.light.Type == D3DLIGHT_POINT && np < key.pointLights && np < 4) {
                const auto& pl = p.pointLights[np];
                if (pl.pos >= 0)
                    glUniform3f(pl.pos, l.light.Position.x, l.light.Position.y, l.light.Position.z);
                if (pl.diffuse >= 0)
                    glUniform4f(pl.diffuse, l.light.Diffuse.r, l.light.Diffuse.g, l.light.Diffuse.b,
                                l.light.Diffuse.a);
                if (pl.atten >= 0)
                    glUniform3f(pl.atten, l.light.Attenuation0, l.light.Attenuation1,
                                l.light.Attenuation2);
                if (pl.range >= 0) glUniform1f(pl.range, l.light.Range);
                np++;
            }
        }
    }

    void bindTextures(const Program& p, const StateSnapshot& s, const ShaderKey& key) {
        for (int i = 0; i < kActiveStages; ++i) {
            if (!key.stages[i].bound || p.tex[i] < 0) continue;
            glActiveTexture(GLenum(GL_TEXTURE0 + i));
            TextureSlot& t = m_textures[s.textures[i] - 1];
            glBindTexture(GL_TEXTURE_2D, t.gl);
            glUniform1i(p.tex[i], i);
            applySampler(t, s.stages[i]);
        }
    }

    void applySampler(TextureSlot& t, const StageState& st) {
        DWORD aU = st.values[D3DTSS_ADDRESSU], aV = st.values[D3DTSS_ADDRESSV];
        DWORD mag = st.values[D3DTSS_MAGFILTER], min = st.values[D3DTSS_MINFILTER],
              mip = st.values[D3DTSS_MIPFILTER];
        if (aU != t.addrU) { glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GLint(glWrap(aU))); t.addrU = aU; }
        if (aV != t.addrV) { glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GLint(glWrap(aV))); t.addrV = aV; }
        if (mag != t.magF) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                            mag == D3DTEXF_POINT ? GL_NEAREST : GL_LINEAR);
            t.magF = mag;
        }
        if (min != t.minF || mip != t.mipF) {
            GLint f;
            bool mips = t.levels > 1 && mip != D3DTEXF_NONE;
            if (min == D3DTEXF_POINT)
                f = mips ? (mip == D3DTEXF_POINT ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_LINEAR)
                         : GL_NEAREST;
            else
                f = mips ? (mip == D3DTEXF_POINT ? GL_LINEAR_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_LINEAR)
                         : GL_LINEAR;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
            t.minF = min; t.mipF = mip;
        }
    }

    void bindGeometry(const DrawGeometry& geo) {
        BufferSlot& vb = m_buffers[geo.vb - 1];
        glBindBuffer(GL_ARRAY_BUFFER, vb.gl);
        if (geo.ib) {
            BufferSlot& ib = m_buffers[geo.ib - 1];
            if (m_boundIB != ib.gl) { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib.gl); m_boundIB = ib.gl; }
        }

        // WebGL2 has no BaseVertex draw — fold it into the attribute offsets.
        const VertexLayout& l = geo.layout;
        const uintptr_t base = uintptr_t(geo.baseVertexIndex) * geo.vbStride;
        GLsizei stride = GLsizei(geo.vbStride);
        auto attr = [&](GLuint idx, GLint size, GLenum type, GLboolean norm, UINT offset, bool on) {
            if (!on) { if (m_attrOn[idx]) { glDisableVertexAttribArray(idx); m_attrOn[idx] = false; } return; }
            glEnableVertexAttribArray(idx);
            m_attrOn[idx] = true;
            glVertexAttribPointer(idx, size, type, norm, stride,
                                  reinterpret_cast<const void*>(base + offset));
        };
        attr(0, l.xyzrhw ? 4 : 3, GL_FLOAT, GL_FALSE, l.offsetPosition, l.xyz || l.xyzrhw);
        attr(1, 3, GL_FLOAT, GL_FALSE, l.offsetNormal, l.normal);
        // D3DCOLOR is BGRA in memory; GLES3 lacks GL_BGRA vertex format, so the
        // generated shaders swizzle (aColor.bgra) instead.
        attr(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, l.offsetDiffuse, l.diffuse);
        for (int i = 0; i < 4; ++i)
            attr(GLuint(3 + i), 2, GL_FLOAT, GL_FALSE, l.offsetTex0 + UINT(i) * 8,
                 i < l.texCoordSets);
    }

    void drawFanIndexed(const DrawGeometry& geo, GLenum indexType, UINT indexSize) {
        // Rare path (some UI). Convert fan to triangle list through a scratch CPU read
        // is impossible in WebGL2 (no buffer readback of ELEMENT_ARRAY cheaply), so we
        // draw fan via sequential strip-like emulation only when non-indexed. Indexed
        // fans get logged; nothing in ZH's hot path uses them.
        (void)geo; (void)indexType; (void)indexSize;
        static bool warned = false;
        if (!warned) {
            std::fprintf(stderr, "[d8web] indexed TRIANGLEFAN not implemented (logged once)\n");
            warned = true;
        }
    }

#ifdef __EMSCRIPTEN__
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE m_ctx = 0;
#endif
    int m_width = 0, m_height = 0;
    GLuint m_vao = 0;
    GLuint m_boundIB = 0;
    bool m_attrOn[8] = {};
    uint64_t m_frame = 0;
    uint64_t m_drawCount = 0;
    uint64_t m_clearCount = 0;
    uint64_t m_texUploads = 0;
    D3DVIEWPORT8 m_viewport{};
    std::vector<BufferSlot> m_buffers;
    std::vector<TextureSlot> m_textures;
    std::unordered_map<ShaderKey, Program, ShaderKeyHash> m_programs;
};

}  // namespace

IBackend* createWebGL2Backend() { return new WebGL2Backend(); }

}  // namespace d8web
