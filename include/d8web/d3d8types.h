// d8web — minimal D3D8 type definitions
// Covers the surface in INTERFACE.md. Not a full d3d8types.h reproduction:
// values match the real API (engine code compiles against them), unused parts omitted.
#pragma once

#include <cstdint>
#include <cstring>


// Coexistence with a real d3d8.h in the same TU (the engine bridge): the real
// header #defines many of the names declared below as macros, which would
// mangle these declarations. Clear them; the bridge re-imports what it needs
// via using-declarations from namespace d8web.
#ifdef DIRECT3D_VERSION
#undef D3D_OK
#undef D3D_SDK_VERSION
#undef D3DADAPTER_DEFAULT
#undef D3DCLEAR_STENCIL
#undef D3DCLEAR_TARGET
#undef D3DCLEAR_ZBUFFER
#undef D3DCOLOR_ARGB
#undef D3DCOLOR_XRGB
#undef D3DCOLORWRITEENABLE_ALPHA
#undef D3DCOLORWRITEENABLE_BLUE
#undef D3DCOLORWRITEENABLE_GREEN
#undef D3DCOLORWRITEENABLE_RED
#undef D3DCREATE_FPU_PRESERVE
#undef D3DCREATE_HARDWARE_VERTEXPROCESSING
#undef D3DCREATE_MIXED_VERTEXPROCESSING
#undef D3DCREATE_SOFTWARE_VERTEXPROCESSING
#undef D3DERR_DEVICELOST
#undef D3DERR_DEVICENOTRESET
#undef D3DERR_INVALIDCALL
#undef D3DERR_NOTAVAILABLE
#undef D3DERR_OUTOFVIDEOMEMORY
#undef D3DFVF_DIFFUSE
#undef D3DFVF_NORMAL
#undef D3DFVF_POSITION_MASK
#undef D3DFVF_SPECULAR
#undef D3DFVF_TEX0
#undef D3DFVF_TEX1
#undef D3DFVF_TEX2
#undef D3DFVF_TEXCOUNT_MASK
#undef D3DFVF_TEXCOUNT_SHIFT
#undef D3DFVF_XYZ
#undef D3DFVF_XYZRHW
#undef D3DLOCK_DISCARD
#undef D3DLOCK_NOOVERWRITE
#undef D3DLOCK_NOSYSLOCK
#undef D3DLOCK_READONLY
#undef D3DPS_VERSION
#undef D3DPTEXTURECAPS_POW2
#undef D3DPTEXTURECAPS_SQUAREONLY
#undef D3DTA_ALPHAREPLICATE
#undef D3DTA_COMPLEMENT
#undef D3DTA_CURRENT
#undef D3DTA_DIFFUSE
#undef D3DTA_SELECTMASK
#undef D3DTA_TEXTURE
#undef D3DTA_TFACTOR
#undef D3DTSS_TCI_PASSTHRU
#undef D3DTSS_TCI_CAMERASPACENORMAL
#undef D3DTSS_TCI_CAMERASPACEPOSITION
#undef D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR
#undef D3DUSAGE_DEPTHSTENCIL
#undef D3DUSAGE_DYNAMIC
#undef D3DUSAGE_RENDERTARGET
#undef D3DUSAGE_SOFTWAREPROCESSING
#undef D3DUSAGE_WRITEONLY
#undef D3DVS_VERSION
#undef D3DTS_WORLD
#endif

// Everything lives in namespace d8web so these definitions can coexist with a
// real d3d8.h (DXVK/mingw) in the same translation unit — the engine bridge
// includes both.
namespace d8web {

// ---- Windows-ish base types (only what D3D8 signatures need) ----
using DWORD = uint32_t;
using WORD = uint16_t;
using BYTE = uint8_t;
using UINT = uint32_t;
using INT = int32_t;
using LONG = int32_t;
using ULONG = uint32_t;
using BOOL = int32_t;
using HRESULT = int32_t;
using HANDLE = void*;
using HWND = void*;
using HMONITOR = void*;
using LPCSTR = const char*;
using LPVOID = void*;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

// HRESULTs
constexpr HRESULT D3D_OK = 0;
constexpr HRESULT D3DERR_INVALIDCALL = static_cast<HRESULT>(0x8876086C);
constexpr HRESULT D3DERR_NOTAVAILABLE = static_cast<HRESULT>(0x8876086A);
constexpr HRESULT D3DERR_OUTOFVIDEOMEMORY = static_cast<HRESULT>(0x8876017C);
constexpr HRESULT D3DERR_DEVICELOST = static_cast<HRESULT>(0x88760868);
constexpr HRESULT D3DERR_DEVICENOTRESET = static_cast<HRESULT>(0x88760869);
constexpr HRESULT E_FAIL_ = static_cast<HRESULT>(0x80004005);

#ifndef SUCCEEDED
#define SUCCEEDED(hr) ((int32_t)(hr) >= 0)
#endif
#ifndef FAILED
#define FAILED(hr) ((int32_t)(hr) < 0)
#endif

constexpr DWORD D3D_SDK_VERSION = 220;
constexpr UINT D3DADAPTER_DEFAULT = 0;

// ---- Formats ----
#define D3DFOURCC(a, b, c, d) \
    ((DWORD)(BYTE)(a) | ((DWORD)(BYTE)(b) << 8) | ((DWORD)(BYTE)(c) << 16) | ((DWORD)(BYTE)(d) << 24))

enum D3DFORMAT : DWORD {
    D3DFMT_UNKNOWN = 0,
    D3DFMT_R8G8B8 = 20,
    D3DFMT_A8R8G8B8 = 21,
    D3DFMT_X8R8G8B8 = 22,
    D3DFMT_R5G6B5 = 23,
    D3DFMT_X1R5G5B5 = 24,
    D3DFMT_A1R5G5B5 = 25,
    D3DFMT_A4R4G4B4 = 26,
    D3DFMT_A8 = 28,
    D3DFMT_X4R4G4B4 = 30,
    D3DFMT_A8P8 = 40,
    D3DFMT_P8 = 41,
    D3DFMT_L8 = 50,
    D3DFMT_A8L8 = 51,
    D3DFMT_V8U8 = 60,
    D3DFMT_D16_LOCKABLE = 70,
    D3DFMT_D32 = 71,
    D3DFMT_D24S8 = 75,
    D3DFMT_D16 = 80,
    D3DFMT_INDEX16 = 101,
    D3DFMT_INDEX32 = 102,
    D3DFMT_DXT1 = D3DFOURCC('D', 'X', 'T', '1'),
    D3DFMT_DXT2 = D3DFOURCC('D', 'X', 'T', '2'),
    D3DFMT_DXT3 = D3DFOURCC('D', 'X', 'T', '3'),
    D3DFMT_DXT4 = D3DFOURCC('D', 'X', 'T', '4'),
    D3DFMT_DXT5 = D3DFOURCC('D', 'X', 'T', '5'),
};

// ---- Enums used by the engine ----
enum D3DDEVTYPE : DWORD { D3DDEVTYPE_HAL = 1, D3DDEVTYPE_REF = 2, D3DDEVTYPE_SW = 3 };

enum D3DPOOL : DWORD { D3DPOOL_DEFAULT = 0, D3DPOOL_MANAGED = 1, D3DPOOL_SYSTEMMEM = 2, D3DPOOL_SCRATCH = 3 };

enum D3DRESOURCETYPE : DWORD {
    D3DRTYPE_SURFACE = 1, D3DRTYPE_TEXTURE = 3,
    D3DRTYPE_VERTEXBUFFER = 6, D3DRTYPE_INDEXBUFFER = 7,
};

enum D3DMULTISAMPLE_TYPE : DWORD { D3DMULTISAMPLE_NONE = 0 };

enum D3DSWAPEFFECT : DWORD { D3DSWAPEFFECT_DISCARD = 1, D3DSWAPEFFECT_FLIP = 2, D3DSWAPEFFECT_COPY = 3 };

enum D3DPRIMITIVETYPE : DWORD {
    D3DPT_POINTLIST = 1, D3DPT_LINELIST = 2, D3DPT_LINESTRIP = 3,
    D3DPT_TRIANGLELIST = 4, D3DPT_TRIANGLESTRIP = 5, D3DPT_TRIANGLEFAN = 6,
};

enum D3DTRANSFORMSTATETYPE : DWORD {
    D3DTS_VIEW = 2, D3DTS_PROJECTION = 3,
    D3DTS_TEXTURE0 = 16, D3DTS_TEXTURE1 = 17, D3DTS_TEXTURE2 = 18, D3DTS_TEXTURE3 = 19,
    D3DTS_TEXTURE4 = 20, D3DTS_TEXTURE5 = 21, D3DTS_TEXTURE6 = 22, D3DTS_TEXTURE7 = 23,
    D3DTS_WORLD = 256,  // D3DTS_WORLDMATRIX(0)
};

enum D3DRENDERSTATETYPE : DWORD {
    D3DRS_ZENABLE = 7,
    D3DRS_FILLMODE = 8,
    D3DRS_SHADEMODE = 9,
    D3DRS_ZWRITEENABLE = 14,
    D3DRS_ALPHATESTENABLE = 15,
    D3DRS_SRCBLEND = 19,
    D3DRS_DESTBLEND = 20,
    D3DRS_CULLMODE = 22,
    D3DRS_ZFUNC = 23,
    D3DRS_ALPHAREF = 24,
    D3DRS_ALPHAFUNC = 25,
    D3DRS_DITHERENABLE = 26,
    D3DRS_ALPHABLENDENABLE = 27,
    D3DRS_FOGENABLE = 28,
    D3DRS_SPECULARENABLE = 29,
    D3DRS_FOGCOLOR = 34,
    D3DRS_FOGTABLEMODE = 35,
    D3DRS_FOGSTART = 36,
    D3DRS_FOGEND = 37,
    D3DRS_FOGDENSITY = 38,
    D3DRS_ZBIAS = 47,
    D3DRS_RANGEFOGENABLE = 48,
    D3DRS_STENCILENABLE = 52,
    D3DRS_STENCILFAIL = 53,
    D3DRS_STENCILZFAIL = 54,
    D3DRS_STENCILPASS = 55,
    D3DRS_STENCILFUNC = 56,
    D3DRS_STENCILREF = 57,
    D3DRS_STENCILMASK = 58,
    D3DRS_STENCILWRITEMASK = 59,
    D3DRS_TEXTUREFACTOR = 60,
    D3DRS_LIGHTING = 137,
    D3DRS_AMBIENT = 139,
    D3DRS_FOGVERTEXMODE = 140,
    D3DRS_COLORVERTEX = 141,
    D3DRS_LOCALVIEWER = 142,
    D3DRS_NORMALIZENORMALS = 143,
    D3DRS_DIFFUSEMATERIALSOURCE = 145,
    D3DRS_SPECULARMATERIALSOURCE = 146,
    D3DRS_AMBIENTMATERIALSOURCE = 147,
    D3DRS_EMISSIVEMATERIALSOURCE = 148,
    D3DRS_COLORWRITEENABLE = 168,
    D3DRS_MAX_SENTINEL = 210,  // storage bound, not a real state
};

enum D3DCULL : DWORD { D3DCULL_NONE = 1, D3DCULL_CW = 2, D3DCULL_CCW = 3 };
enum D3DFILLMODE : DWORD { D3DFILL_POINT = 1, D3DFILL_WIREFRAME = 2, D3DFILL_SOLID = 3 };
enum D3DSHADEMODE : DWORD { D3DSHADE_FLAT = 1, D3DSHADE_GOURAUD = 2 };

enum D3DCMPFUNC : DWORD {
    D3DCMP_NEVER = 1, D3DCMP_LESS = 2, D3DCMP_EQUAL = 3, D3DCMP_LESSEQUAL = 4,
    D3DCMP_GREATER = 5, D3DCMP_NOTEQUAL = 6, D3DCMP_GREATEREQUAL = 7, D3DCMP_ALWAYS = 8,
};

enum D3DBLEND : DWORD {
    D3DBLEND_ZERO = 1, D3DBLEND_ONE = 2, D3DBLEND_SRCCOLOR = 3, D3DBLEND_INVSRCCOLOR = 4,
    D3DBLEND_SRCALPHA = 5, D3DBLEND_INVSRCALPHA = 6, D3DBLEND_DESTALPHA = 7,
    D3DBLEND_INVDESTALPHA = 8, D3DBLEND_DESTCOLOR = 9, D3DBLEND_INVDESTCOLOR = 10,
    D3DBLEND_SRCALPHASAT = 11,
};

enum D3DFOGMODE : DWORD { D3DFOG_NONE = 0, D3DFOG_EXP = 1, D3DFOG_EXP2 = 2, D3DFOG_LINEAR = 3 };

enum D3DMATERIALCOLORSOURCE : DWORD { D3DMCS_MATERIAL = 0, D3DMCS_COLOR1 = 1, D3DMCS_COLOR2 = 2 };

enum D3DLIGHTTYPE : DWORD { D3DLIGHT_POINT = 1, D3DLIGHT_SPOT = 2, D3DLIGHT_DIRECTIONAL = 3 };

// Texture stage states
enum D3DTEXTURESTAGESTATETYPE : DWORD {
    D3DTSS_COLOROP = 1,
    D3DTSS_COLORARG1 = 2,
    D3DTSS_COLORARG2 = 3,
    D3DTSS_ALPHAOP = 4,
    D3DTSS_ALPHAARG1 = 5,
    D3DTSS_ALPHAARG2 = 6,
    D3DTSS_TEXCOORDINDEX = 11,
    D3DTSS_ADDRESSU = 13,
    D3DTSS_ADDRESSV = 14,
    D3DTSS_BORDERCOLOR = 15,
    D3DTSS_MAGFILTER = 16,
    D3DTSS_MINFILTER = 17,
    D3DTSS_MIPFILTER = 18,
    D3DTSS_MIPMAPLODBIAS = 19,
    D3DTSS_MAXMIPLEVEL = 20,
    D3DTSS_MAXANISOTROPY = 21,
    D3DTSS_TEXTURETRANSFORMFLAGS = 24,
    D3DTSS_MAX_SENTINEL = 33,  // storage bound
};

enum D3DTEXTUREOP : DWORD {
    D3DTOP_DISABLE = 1, D3DTOP_SELECTARG1 = 2, D3DTOP_SELECTARG2 = 3,
    D3DTOP_MODULATE = 4, D3DTOP_MODULATE2X = 5, D3DTOP_MODULATE4X = 6,
    D3DTOP_ADD = 7, D3DTOP_ADDSIGNED = 8, D3DTOP_SUBTRACT = 10,
    D3DTOP_BLENDDIFFUSEALPHA = 12, D3DTOP_BLENDTEXTUREALPHA = 13,
    D3DTOP_BLENDCURRENTALPHA = 16, D3DTOP_DOTPRODUCT3 = 24,
};

// TA_* texture args (low bits select source)
constexpr DWORD D3DTA_DIFFUSE = 0x0;
constexpr DWORD D3DTA_CURRENT = 0x1;
constexpr DWORD D3DTA_TEXTURE = 0x2;
constexpr DWORD D3DTA_TFACTOR = 0x3;
constexpr DWORD D3DTA_SELECTMASK = 0xF;
constexpr DWORD D3DTA_COMPLEMENT = 0x10;
constexpr DWORD D3DTA_ALPHAREPLICATE = 0x20;

// TEXCOORDINDEX texgen selectors (high 16 bits of D3DTSS_TEXCOORDINDEX)
constexpr DWORD D3DTSS_TCI_PASSTHRU = 0x00000;
constexpr DWORD D3DTSS_TCI_CAMERASPACENORMAL = 0x10000;
constexpr DWORD D3DTSS_TCI_CAMERASPACEPOSITION = 0x20000;
constexpr DWORD D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR = 0x30000;

enum D3DTEXTURETRANSFORMFLAGS : DWORD {
    D3DTTFF_DISABLE = 0, D3DTTFF_COUNT1 = 1, D3DTTFF_COUNT2 = 2,
    D3DTTFF_COUNT3 = 3, D3DTTFF_COUNT4 = 4, D3DTTFF_PROJECTED = 256,
};

enum D3DTEXTUREADDRESS : DWORD {
    D3DTADDRESS_WRAP = 1, D3DTADDRESS_MIRROR = 2, D3DTADDRESS_CLAMP = 3, D3DTADDRESS_BORDER = 4,
};

enum D3DTEXTUREFILTERTYPE : DWORD {
    D3DTEXF_NONE = 0, D3DTEXF_POINT = 1, D3DTEXF_LINEAR = 2, D3DTEXF_ANISOTROPIC = 3,
};

// ---- FVF ----
constexpr DWORD D3DFVF_XYZ = 0x002;
constexpr DWORD D3DFVF_XYZRHW = 0x004;
constexpr DWORD D3DFVF_NORMAL = 0x010;
constexpr DWORD D3DFVF_DIFFUSE = 0x040;
constexpr DWORD D3DFVF_SPECULAR = 0x080;
constexpr DWORD D3DFVF_TEXCOUNT_MASK = 0xF00;
constexpr DWORD D3DFVF_TEXCOUNT_SHIFT = 8;
constexpr DWORD D3DFVF_TEX0 = 0x000;
constexpr DWORD D3DFVF_TEX1 = 0x100;
constexpr DWORD D3DFVF_TEX2 = 0x200;
constexpr DWORD D3DFVF_POSITION_MASK = 0x00E;

// ---- Usage / Lock / Clear flags ----
constexpr DWORD D3DUSAGE_RENDERTARGET = 0x01;
constexpr DWORD D3DUSAGE_DEPTHSTENCIL = 0x02;
constexpr DWORD D3DUSAGE_WRITEONLY = 0x08;
constexpr DWORD D3DUSAGE_SOFTWAREPROCESSING = 0x10;
constexpr DWORD D3DUSAGE_DYNAMIC = 0x200;

constexpr DWORD D3DLOCK_READONLY = 0x10;
constexpr DWORD D3DLOCK_DISCARD = 0x2000;
constexpr DWORD D3DLOCK_NOOVERWRITE = 0x1000;
constexpr DWORD D3DLOCK_NOSYSLOCK = 0x800;

constexpr DWORD D3DCLEAR_TARGET = 0x1;
constexpr DWORD D3DCLEAR_ZBUFFER = 0x2;
constexpr DWORD D3DCLEAR_STENCIL = 0x4;

constexpr DWORD D3DCOLORWRITEENABLE_RED = 0x1;
constexpr DWORD D3DCOLORWRITEENABLE_GREEN = 0x2;
constexpr DWORD D3DCOLORWRITEENABLE_BLUE = 0x4;
constexpr DWORD D3DCOLORWRITEENABLE_ALPHA = 0x8;

// Device create behavior flags
constexpr DWORD D3DCREATE_SOFTWARE_VERTEXPROCESSING = 0x20;
constexpr DWORD D3DCREATE_HARDWARE_VERTEXPROCESSING = 0x40;
constexpr DWORD D3DCREATE_MIXED_VERTEXPROCESSING = 0x80;
constexpr DWORD D3DCREATE_FPU_PRESERVE = 0x02;

// ---- Color ----
using D3DCOLOR = DWORD;
constexpr D3DCOLOR D3DCOLOR_ARGB(BYTE a, BYTE r, BYTE g, BYTE b) {
    return (DWORD(a) << 24) | (DWORD(r) << 16) | (DWORD(g) << 8) | DWORD(b);
}
constexpr D3DCOLOR D3DCOLOR_XRGB(BYTE r, BYTE g, BYTE b) { return D3DCOLOR_ARGB(0xFF, r, g, b); }

// ---- Structs ----
struct D3DCOLORVALUE { float r, g, b, a; };
struct D3DVECTOR { float x, y, z; };

struct D3DMATRIX {
    union {
        struct {
            float _11, _12, _13, _14;
            float _21, _22, _23, _24;
            float _31, _32, _33, _34;
            float _41, _42, _43, _44;
        };
        float m[4][4];
    };
};

struct D3DVIEWPORT8 {
    DWORD X, Y, Width, Height;
    float MinZ, MaxZ;
};

struct D3DMATERIAL8 {
    D3DCOLORVALUE Diffuse, Ambient, Specular, Emissive;
    float Power;
};

struct D3DLIGHT8 {
    D3DLIGHTTYPE Type;
    D3DCOLORVALUE Diffuse, Specular, Ambient;
    D3DVECTOR Position, Direction;
    float Range, Falloff;
    float Attenuation0, Attenuation1, Attenuation2;
    float Theta, Phi;
};

struct D3DPRESENT_PARAMETERS {
    UINT BackBufferWidth, BackBufferHeight;
    D3DFORMAT BackBufferFormat;
    UINT BackBufferCount;
    D3DMULTISAMPLE_TYPE MultiSampleType;
    D3DSWAPEFFECT SwapEffect;
    HWND hDeviceWindow;
    BOOL Windowed;
    BOOL EnableAutoDepthStencil;
    D3DFORMAT AutoDepthStencilFormat;
    DWORD Flags;
    UINT FullScreen_RefreshRateInHz;
    UINT FullScreen_PresentationInterval;
};

struct D3DDISPLAYMODE {
    UINT Width, Height, RefreshRate;
    D3DFORMAT Format;
};

struct D3DSURFACE_DESC {
    D3DFORMAT Format;
    D3DRESOURCETYPE Type;
    DWORD Usage;
    D3DPOOL Pool;
    UINT Size;
    D3DMULTISAMPLE_TYPE MultiSampleType;
    UINT Width, Height;
};

struct D3DLOCKED_RECT {
    INT Pitch;
    void* pBits;
};

struct RECT_ { LONG left, top, right, bottom; };
using RECT = RECT_;
struct POINT_ { LONG x, y; };
using POINT = POINT_;

struct D3DADAPTER_IDENTIFIER8 {
    char Driver[512];
    char Description[512];
    DWORD DriverVersionLowPart, DriverVersionHighPart;
    DWORD VendorId, DeviceId, SubSysId, Revision;
    BYTE DeviceIdentifier[16];
    DWORD WHQLLevel;
};

// Caps — engine reads a handful of fields; keep layout minimal but stable
struct D3DCAPS8 {
    D3DDEVTYPE DeviceType;
    UINT AdapterOrdinal;
    DWORD Caps, Caps2, Caps3;
    DWORD PresentationIntervals;
    DWORD DevCaps;
    DWORD RasterCaps;
    DWORD TextureCaps;
    DWORD TextureFilterCaps;
    DWORD TextureAddressCaps;
    DWORD MaxTextureWidth, MaxTextureHeight;
    DWORD MaxTextureBlendStages;
    DWORD MaxSimultaneousTextures;
    DWORD VertexProcessingCaps;
    DWORD MaxActiveLights;
    DWORD MaxUserClipPlanes;
    DWORD MaxVertexBlendMatrices;
    DWORD MaxPrimitiveCount;
    DWORD MaxVertexIndex;
    DWORD MaxStreams;
    DWORD MaxStreamStride;
    DWORD VertexShaderVersion;
    DWORD MaxVertexShaderConst;
    DWORD PixelShaderVersion;
    float MaxPixelShaderValue;
};

// Texture caps bits the engine checks
constexpr DWORD D3DPTEXTURECAPS_SQUAREONLY = 0x20;
constexpr DWORD D3DPTEXTURECAPS_POW2 = 0x02;

// Shader version macros — we report 0 to force FFP paths
constexpr DWORD D3DVS_VERSION(BYTE major, BYTE minor) { return 0xFFFE0000u | (DWORD(major) << 8) | minor; }
constexpr DWORD D3DPS_VERSION(BYTE major, BYTE minor) { return 0xFFFF0000u | (DWORD(major) << 8) | minor; }

}  // namespace d8web
