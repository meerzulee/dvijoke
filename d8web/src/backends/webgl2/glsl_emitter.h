// d8web WebGL2 backend — GLSL 300 es emitter
// ShaderKey in, vertex+fragment source out. The WebGPU backend will have a sibling
// WGSL emitter consuming the same key; keep this file free of GL calls.
#pragma once

#include "../../core/shaderkey.h"

#include <string>

namespace d8web::webgl2 {

struct ShaderSource {
    std::string vertex;
    std::string fragment;
};

ShaderSource emitGLSL(const ShaderKey& key);

}  // namespace d8web::webgl2
