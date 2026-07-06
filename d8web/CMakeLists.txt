cmake_minimum_required(VERSION 3.20)
project(d8web CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Library: D3D8 frontend + core seam + WebGL2 backend
add_library(d8web STATIC
    src/frontend/device.cpp
    src/backends/webgl2/glsl_emitter.cpp
    src/backends/webgl2/gl_backend.cpp
)
target_include_directories(d8web PUBLIC include PRIVATE src)

if(EMSCRIPTEN)
    # MAX_WEBGL_VERSION=2 exposes GLES3 entry points; contexts are created by the
    # backend itself via the html5.h API.
    # GROWABLE_ARRAYBUFFERS=0: Chrome rejects resizable ArrayBuffer views in WebGL
    # calls (bufferSubData "must not be resizable"); force legacy detach-and-copy growth.
    target_link_options(d8web INTERFACE
        "-sMAX_WEBGL_VERSION=2" "-sMIN_WEBGL_VERSION=2" "-sALLOW_MEMORY_GROWTH=1"
        "-sGROWABLE_ARRAYBUFFERS=0")
endif()

# Examples, each a self-contained D3D8 program
function(d8web_example name path)
    add_executable(${name} ${path})
    target_link_libraries(${name} PRIVATE d8web)
    if(EMSCRIPTEN)
        set_target_properties(${name} PROPERTIES SUFFIX ".html")
        target_link_options(${name} PRIVATE
            "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/examples/shell.html")
    endif()
endfunction()

d8web_example(cube examples/01-cube/main.cpp)      # FFP lighting, vertex colors, culling
d8web_example(texquad examples/02-texquad/main.cpp) # textures, alpha test, DrawPrimitiveUP
d8web_example(ui examples/03-ui/main.cpp)           # XYZRHW screen-space UI, alpha blending
