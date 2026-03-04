#include "tjs.h"
#include "ncbind.hpp"
#include "ScriptMgnIntf.h"
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <vector>
#include <cstring>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl3.h>

#define NCB_MODULE_NAME TJS_W("krkrgles.dll")

// Live2D model's internal FBO — published by krkrlive2d.cpp
struct Live2DRenderTarget {
    GLuint fbo;
    GLsizei width;
    GLsizei height;
};
extern Live2DRenderTarget g_live2dRenderTarget;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace {

inline tjs_int ToInt(const tTJSVariant &v, tjs_int fallback = 0) {
    switch (v.Type()) {
    case tvtInteger: return static_cast<tjs_int>(v);
    case tvtReal:    return static_cast<tjs_int>(static_cast<tjs_real>(v));
    default:         return fallback;
    }
}

inline tjs_int NormalizeExtent(tjs_int v, tjs_int fb) { return v > 0 ? v : fb; }

using ModuleName = std::basic_string<tjs_char>;

inline ModuleName NormalizeModuleName(tjs_int n, tTJSVariant **p) {
    if (n <= 0 || !p || !p[0] || p[0]->Type() == tvtVoid) return TJS_W("live2d");
    ttstr raw(*p[0]);
    ModuleName out(raw.c_str());
    for (auto &ch : out)
        if (ch >= 'A' && ch <= 'Z') ch += 32;
    if (out.empty()) out = TJS_W("live2d");
    return out;
}

inline void SetResultObject(tTJSVariant *r, iTJSDispatch2 *o) {
    if (r && o) *r = tTJSVariant(o, o);
}

inline void SetObjectProperty(iTJSDispatch2 *o, const tjs_char *n, const tTJSVariant &v) {
    if (!o || !n) return;
    tTJSVariant copy(v);
    o->PropSet(TJS_MEMBERENSURE, n, nullptr, &copy, o);
}

inline void SetObjectMethod(iTJSDispatch2 *o, const tjs_char *n,
                            tTJSNativeClassMethodCallback cb) {
    if (!o || !n || !cb) return;
    iTJSDispatch2 *m = TJSCreateNativeClassMethod(cb);
    if (!m) return;
    tTJSVariant v(m, m);
    o->PropSet(TJS_MEMBERENSURE, n, nullptr, &v, o);
    m->Release();
}

// ---------------------------------------------------------------------------
// KTX1 texture loader — uploads compressed or uncompressed KTX to GL
// ---------------------------------------------------------------------------
struct KtxHeader {
    uint8_t  identifier[12];
    uint32_t endianness;
    uint32_t glType;
    uint32_t glTypeSize;
    uint32_t glFormat;
    uint32_t glInternalFormat;
    uint32_t glBaseInternalFormat;
    uint32_t pixelWidth;
    uint32_t pixelHeight;
    uint32_t pixelDepth;
    uint32_t numberOfArrayElements;
    uint32_t numberOfFaces;
    uint32_t numberOfMipmapLevels;
    uint32_t bytesOfKeyValueData;
};

} // end anonymous namespace

extern "C" GLuint LoadKtxTexture(const uint8_t *data, size_t dataSize) {
    if (dataSize < sizeof(KtxHeader)) return 0;
    const KtxHeader *hdr = reinterpret_cast<const KtxHeader *>(data);

    static const uint8_t KTX_MAGIC[12] = {
        0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A
    };
    if (std::memcmp(hdr->identifier, KTX_MAGIC, 12) != 0) return 0;

    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (!tex) return 0;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    const bool compressed = (hdr->glType == 0 && hdr->glFormat == 0);
    const uint8_t *ptr = data + sizeof(KtxHeader) + hdr->bytesOfKeyValueData;
    uint32_t w = hdr->pixelWidth, h = hdr->pixelHeight;
    uint32_t levels = hdr->numberOfMipmapLevels;
    if (levels == 0) levels = 1;

    spdlog::debug("krkrgles: KTX {}x{} internalFmt=0x{:04X} fmt=0x{:04X} type=0x{:04X} compressed={} levels={}",
                  w, h, hdr->glInternalFormat, hdr->glFormat, hdr->glType, compressed, levels);

    for (uint32_t level = 0; level < levels; ++level) {
        if (ptr + 4 > data + dataSize) break;
        uint32_t imageSize;
        std::memcpy(&imageSize, ptr, 4);
        ptr += 4;
        if (ptr + imageSize > data + dataSize) break;

        if (compressed) {
            glCompressedTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level),
                                  hdr->glInternalFormat,
                                  static_cast<GLsizei>(w), static_cast<GLsizei>(h),
                                  0, static_cast<GLsizei>(imageSize), ptr);
        } else {
            glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level),
                         static_cast<GLint>(hdr->glInternalFormat),
                         static_cast<GLsizei>(w), static_cast<GLsizei>(h),
                         0, hdr->glFormat, hdr->glType, ptr);
        }
        ptr += (imageSize + 3) & ~3u;
        w = (w > 1) ? w / 2 : 1;
        h = (h > 1) ? h / 2 : 1;
    }

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        spdlog::warn("krkrgles: KTX upload GL error 0x{:04X}, falling back to blank", err);
        uint32_t blankW = hdr->pixelWidth, blankH = hdr->pixelHeight;
        if (blankW > 2048) { blankH = blankH * 2048 / blankW; blankW = 2048; }
        std::vector<uint8_t> blank(blankW * blankH * 4, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                     static_cast<GLsizei>(blankW), static_cast<GLsizei>(blankH),
                     0, GL_RGBA, GL_UNSIGNED_BYTE, blank.data());
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

namespace {

// ---------------------------------------------------------------------------
// FBO manager — offscreen render target for Live2D
// ---------------------------------------------------------------------------
class OffscreenFBO {
public:
    bool EnsureSize(GLsizei w, GLsizei h) {
        if (fbo_ && w == width_ && h == height_) return true;
        Destroy();
        width_ = w;
        height_ = h;

        glGenFramebuffers(1, &fbo_);
        glGenTextures(1, &colorTex_);
        glBindTexture(GL_TEXTURE_2D, colorTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);

        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, colorTex_, 0);
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        if (status != GL_FRAMEBUFFER_COMPLETE) {
            spdlog::error("krkrgles: FBO incomplete (status=0x{:04X})", status);
            Destroy();
            return false;
        }
        spdlog::debug("krkrgles: FBO created {}x{}", w, h);
        return true;
    }

    void Bind() {
        if (!fbo_) return;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glViewport(0, 0, width_, height_);
        glClearColor(0.f, 0.f, 0.f, 0.f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    void Unbind() {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo_));
    }

    bool ReadPixels(std::vector<uint8_t> &buf) {
        if (!fbo_) return false;
        buf.resize(static_cast<size_t>(width_) * height_ * 4);
        GLint prev;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev));
        return true;
    }

    void Destroy() {
        if (colorTex_) { glDeleteTextures(1, &colorTex_); colorTex_ = 0; }
        if (fbo_) { glDeleteFramebuffers(1, &fbo_); fbo_ = 0; }
        width_ = height_ = 0;
    }

    GLuint GetFBO() const { return fbo_; }
    GLsizei GetWidth() const { return width_; }
    GLsizei GetHeight() const { return height_; }

    ~OffscreenFBO() { Destroy(); }

private:
    GLuint fbo_ = 0;
    GLuint colorTex_ = 0;
    GLsizei width_ = 0, height_ = 0;
    GLint prevFbo_ = 0;
};

} // end anonymous namespace (for exported symbols below)

// ---------------------------------------------------------------------------
// Find the first Layer object among the callback parameters.
// The game may pass (intFlag, layer, ...) instead of (layer).
// ---------------------------------------------------------------------------
static iTJSDispatch2 *FindLayerInParams(tjs_int n, tTJSVariant **p) {
    if (!p) return nullptr;
    for (tjs_int i = 0; i < n; ++i) {
        if (p[i] && p[i]->Type() == tvtObject) {
            iTJSDispatch2 *obj = p[i]->AsObjectNoAddRef();
            if (!obj) continue;
            tTJSVariant test;
            if (TJS_SUCCEEDED(obj->PropGet(0, TJS_W("imageWidth"), nullptr, &test, obj)))
                return obj;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// GPU fast path: blit FBO → Layer's native GL texture via glBlitFramebuffer.
// Returns true if GPU path was used, false if not available.
// ---------------------------------------------------------------------------
static bool CopyFBOToLayerGPU(GLuint srcFbo, GLsizei srcW, GLsizei srcH,
                              iTJSDispatch2 *layer) {
    tTJSVariant vTex, vIW, vIH;
    if (TJS_FAILED(layer->PropGet(0, TJS_W("mainImageGLTexture"), nullptr, &vTex, layer)))
        return false;
    GLuint layerTexId = static_cast<GLuint>(static_cast<tTVInteger>(vTex));
    if (layerTexId == 0) return false;

    layer->PropGet(0, TJS_W("mainImageGLTextureInternalWidth"), nullptr, &vIW, layer);
    layer->PropGet(0, TJS_W("mainImageGLTextureInternalHeight"), nullptr, &vIH, layer);
    GLsizei intW = static_cast<GLsizei>(static_cast<tTVInteger>(vIW));
    GLsizei intH = static_cast<GLsizei>(static_cast<tTVInteger>(vIH));
    if (intW <= 0 || intH <= 0) return false;

    tTJSVariant vW, vH;
    layer->PropGet(0, TJS_W("imageWidth"), nullptr, &vW, layer);
    layer->PropGet(0, TJS_W("imageHeight"), nullptr, &vH, layer);
    GLsizei layerW = static_cast<GLsizei>(static_cast<tTVInteger>(vW));
    GLsizei layerH = static_cast<GLsizei>(static_cast<tTVInteger>(vH));

    GLint prevFbo;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

    GLuint dstFbo = 0;
    glGenFramebuffers(1, &dstFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, layerTexId, 0);

    if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
        glDeleteFramebuffers(1, &dstFbo);
        return false;
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFbo);

    GLsizei blitW = (layerW < srcW) ? layerW : srcW;
    GLsizei blitH = (layerH < srcH) ? layerH : srcH;

    // Source FBO is bottom-up (OGL convention); Layer texture in GPU mode
    // is also OGL convention (bottom-up), so no Y-flip needed.
    glBlitFramebuffer(0, 0, blitW, blitH,
                      0, 0, blitW, blitH,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glDeleteFramebuffers(1, &dstFbo);

    tjs_uint hint = 0;
    layer->FuncCall(0, TJS_W("invalidateGLTextureCache"), &hint, nullptr, 0, nullptr, layer);

    tTJSVariant vx(static_cast<tjs_int>(0)), vy(static_cast<tjs_int>(0)),
                vw(static_cast<tjs_int>(blitW)), vh(static_cast<tjs_int>(blitH));
    tTJSVariant *args[] = { &vx, &vy, &vw, &vh };
    hint = 0;
    layer->FuncCall(0, TJS_W("update"), &hint, nullptr, 4, args, layer);
    return true;
}

// ---------------------------------------------------------------------------
// CPU fallback: read pixels from GL FBO → TJS Layer bitmap.
// GL outputs RGBA bottom-up; krkr2 Layer CPU buffer uses BGRA top-down.
// ---------------------------------------------------------------------------
static bool CopyFBOToLayerCPU(GLuint fbo, GLsizei srcW, GLsizei srcH,
                              iTJSDispatch2 *layer) {
    tTJSVariant vW, vH, vBuf, vPitch;
    layer->PropGet(0, TJS_W("imageWidth"), nullptr, &vW, layer);
    layer->PropGet(0, TJS_W("imageHeight"), nullptr, &vH, layer);
    layer->PropGet(0, TJS_W("mainImageBufferForWrite"), nullptr, &vBuf, layer);
    layer->PropGet(0, TJS_W("mainImageBufferPitch"), nullptr, &vPitch, layer);

    tjs_int layerW = static_cast<tjs_int>(vW);
    tjs_int layerH = static_cast<tjs_int>(vH);
    auto *dst = reinterpret_cast<uint8_t *>(
        static_cast<tjs_intptr_t>(static_cast<tTVInteger>(vBuf)));
    tjs_int pitch = static_cast<tjs_int>(vPitch);
    if (!dst || layerW <= 0 || layerH <= 0) return false;

    std::vector<uint8_t> rgba(static_cast<size_t>(srcW) * srcH * 4);
    GLint prevFbo;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glReadPixels(0, 0, srcW, srcH, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));

    tjs_int copyW = (layerW < srcW) ? layerW : srcW;
    tjs_int copyH = (layerH < srcH) ? layerH : srcH;

    for (tjs_int y = 0; y < copyH; ++y) {
        const uint8_t *srcRow = rgba.data() +
            static_cast<size_t>(srcH - 1 - y) * srcW * 4;
        uint8_t *dstRow = dst + static_cast<size_t>(y) * pitch;
        for (tjs_int x = 0; x < copyW; ++x) {
            dstRow[x * 4 + 0] = srcRow[x * 4 + 2]; // B
            dstRow[x * 4 + 1] = srcRow[x * 4 + 1]; // G
            dstRow[x * 4 + 2] = srcRow[x * 4 + 0]; // R
            dstRow[x * 4 + 3] = srcRow[x * 4 + 3]; // A
        }
    }

    tTJSVariant vx(static_cast<tjs_int>(0)), vy(static_cast<tjs_int>(0)),
                vw(copyW), vh(copyH);
    tTJSVariant *args[] = { &vx, &vy, &vw, &vh };
    tjs_uint hint = 0;
    layer->FuncCall(0, TJS_W("update"), &hint, nullptr, 4, args, layer);
    return true;
}

// ---------------------------------------------------------------------------
// Copy FBO → Layer with automatic GPU/CPU path selection.
// ---------------------------------------------------------------------------
bool CopyFBOToLayer(GLuint fbo, GLsizei srcW, GLsizei srcH,
                    iTJSDispatch2 *layer) {
    if (!fbo || srcW <= 0 || srcH <= 0 || !layer) return false;
    if (CopyFBOToLayerGPU(fbo, srcW, srcH, layer)) return true;
    return CopyFBOToLayerCPU(fbo, srcW, srcH, layer);
}

// Global registered Layer — set by entryUpdateObject, accessed by krkrlive2d.cpp
static iTJSDispatch2 *g_registeredLayer = nullptr;
iTJSDispatch2 *KrkrGLES_GetRegisteredLayer() { return g_registeredLayer; }

namespace { // reopen anonymous namespace

// Wrapper for OffscreenFBO (used when the module's own FBO has content)
static bool CopyPixelsToLayer(OffscreenFBO &fbo, tjs_int numparams,
                              tTJSVariant **param) {
    iTJSDispatch2 *layer = FindLayerInParams(numparams, param);
    if (!layer) return false;
    return CopyFBOToLayer(fbo.GetFBO(), fbo.GetWidth(), fbo.GetHeight(), layer);
}

// ---------------------------------------------------------------------------
// TJS expression helpers
// ---------------------------------------------------------------------------
static tjs_error CreateObjectByExpression(tTJSVariant *result,
                                          const tjs_char *expr,
                                          const char *tag) {
    if (!result || !expr) return TJS_E_FAIL;
    try { TVPExecuteExpression(ttstr(expr), result); }
    catch (...) {
        spdlog::error("krkrgles: {} eval '{}' failed", tag, ttstr(expr).AsStdString());
        result->Clear();
        return TJS_E_FAIL;
    }
    if (result->Type() != tvtObject || result->AsObjectNoAddRef() == nullptr) {
        result->Clear();
        return TJS_E_FAIL;
    }
    return TJS_S_OK;
}

static void InvokeLoadIfPresent(tTJSVariant &obj, tjs_int n, tTJSVariant **p,
                                const char *tag) {
    if (n <= 0 || !p) return;
    iTJSDispatch2 *d = obj.AsObjectNoAddRef();
    if (!d) return;
    tjs_uint hint = 0;
    d->FuncCall(0, TJS_W("load"), &hint, nullptr, n, p, d);
}

// ---------------------------------------------------------------------------
// Capture-callback invoker (capture → callback → render → copyLayer)
// ---------------------------------------------------------------------------
static tjs_error InvokeCaptureCallback(const char *tag, tjs_int w, tjs_int h,
                                       tjs_int n, tTJSVariant **p) {
    if (n <= 1 || !p || !p[1] || p[1]->Type() != tvtObject) return TJS_S_OK;
    tTJSVariantClosure cb = p[1]->AsObjectClosureNoAddRef();
    if (!cb.Object) return TJS_S_OK;
    tTJSVariant target;
    if (n > 0 && p[0]) target = *p[0];
    tTJSVariant wv(w), hv(h), uv, fv;
    if (n > 2 && p[2]) uv = *p[2];
    if (n > 3 && p[3]) fv = *p[3];
    tTJSVariant *args[] = { &target, &wv, &hv, &uv, &fv };
    tjs_int argc = (n > 3) ? 5 : 4;
    return cb.FuncCall(0, nullptr, nullptr, nullptr, argc, args, nullptr);
}

// ---------------------------------------------------------------------------
// Fallback dictionary module (when Cubism adaptor unavailable)
// ---------------------------------------------------------------------------
static tjs_error ReturnTrueCb(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
    if (r) *r = true; return TJS_S_OK;
}

static tjs_error ReturnFirstArgOrTrueCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, iTJSDispatch2 *) {
    if (!r) return TJS_S_OK;
    *r = (n > 0 && p) ? *p[0] : tTJSVariant(true);
    return TJS_S_OK;
}

static tjs_error DictSetScreenSizeCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, iTJSDispatch2 *obj) {
    if (obj && p) {
        if (n > 0) SetObjectProperty(obj, TJS_W("screenWidth"), *p[0]);
        if (n > 1) SetObjectProperty(obj, TJS_W("screenHeight"), *p[1]);
    }
    if (r) *r = true; return TJS_S_OK;
}

static tjs_error DictCreateModelCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, iTJSDispatch2 *) {
    tTJSVariant model;
    tjs_error er = CreateObjectByExpression(&model, TJS_W("new Live2DModel()"), "fallback.createModel");
    if (TJS_FAILED(er)) { if (r) r->Clear(); return er; }
    InvokeLoadIfPresent(model, n, p, "fallback.createModel");
    if (r) *r = model;
    return TJS_S_OK;
}

static tjs_error DictCreateMatrixCb(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
    return CreateObjectByExpression(r, TJS_W("new Live2DMatrix()"), "fallback.createMatrix");
}

static tjs_error DictCreateDeviceCb(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
    return CreateObjectByExpression(r, TJS_W("new Live2DDevice()"), "fallback.createDevice");
}

static tjs_error DictEntryUpdateObjectCb(tTJSVariant *r, tjs_int n,
                                         tTJSVariant **p, iTJSDispatch2 *) {
    if (n > 0 && p) {
        iTJSDispatch2 *layer = FindLayerInParams(n, p);
        if (layer) g_registeredLayer = layer;
    }
    if (r) *r = true; return TJS_S_OK;
}

static tjs_error CreateFallbackModuleObject(tTJSVariant *result, tjs_int w, tjs_int h) {
    spdlog::warn("krkrgles: using fallback module ({}x{})", w, h);
    iTJSDispatch2 *dict = TJSCreateDictionaryObject();
    if (!dict) { if (result) result->Clear(); return TJS_E_FAIL; }
    SetObjectProperty(dict, TJS_W("screenWidth"), tTJSVariant(w));
    SetObjectProperty(dict, TJS_W("screenHeight"), tTJSVariant(h));
    SetObjectMethod(dict, TJS_W("entryUpdateObject"), DictEntryUpdateObjectCb);
    SetObjectMethod(dict, TJS_W("setScreenSize"), DictSetScreenSizeCb);
    SetObjectMethod(dict, TJS_W("makeCurrent"), ReturnTrueCb);
    SetObjectMethod(dict, TJS_W("beginScene"), ReturnTrueCb);
    SetObjectMethod(dict, TJS_W("endScene"), ReturnTrueCb);
    SetObjectMethod(dict, TJS_W("finalize"), ReturnTrueCb);
    SetObjectMethod(dict, TJS_W("render"), ReturnTrueCb);
    SetObjectMethod(dict, TJS_W("glesEntry"), ReturnTrueCb);
    SetObjectMethod(dict, TJS_W("glesRemove"), ReturnTrueCb);
    SetObjectMethod(dict, TJS_W("capture"), ReturnFirstArgOrTrueCb);
    SetObjectMethod(dict, TJS_W("captureScreen"), ReturnFirstArgOrTrueCb);
    SetObjectMethod(dict, TJS_W("glesCapture"), ReturnFirstArgOrTrueCb);
    SetObjectMethod(dict, TJS_W("glesCaptureScreen"), ReturnFirstArgOrTrueCb);
    SetObjectMethod(dict, TJS_W("copyLayer"), ReturnTrueCb);
    SetObjectMethod(dict, TJS_W("glesCopyLayer"), ReturnTrueCb);
    SetObjectMethod(dict, TJS_W("drawLayer"), ReturnTrueCb);
    SetObjectMethod(dict, TJS_W("glesDrawLayer"), ReturnTrueCb);
    SetObjectMethod(dict, TJS_W("drawAffineGLES"), ReturnTrueCb);
    SetObjectMethod(dict, TJS_W("createModel"), DictCreateModelCb);
    SetObjectMethod(dict, TJS_W("createMatrix"), DictCreateMatrixCb);
    SetObjectMethod(dict, TJS_W("createDevice"), DictCreateDeviceCb);
    SetResultObject(result, dict);
    dict->Release();
    return TJS_S_OK;
}

// ---------------------------------------------------------------------------
// GLESModule — holds per-module FBO + rendering state
// ---------------------------------------------------------------------------
class GLESModule {
public:
    GLESModule() = default;
    ~GLESModule() { fbo_.Destroy(); }

    OffscreenFBO &GetFBO() { return fbo_; }

    static tjs_error entryUpdateObjectCb(tTJSVariant *r, tjs_int n,
                                         tTJSVariant **p, GLESModule *) {
        if (n > 0 && p) {
            iTJSDispatch2 *layer = FindLayerInParams(n, p);
            if (layer) {
                g_registeredLayer = layer;
            }
        }
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error setScreenSizeCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESModule *s) {
        if (!s || !p) return TJS_S_OK;
        if (n > 0) s->screenWidth_ = ToInt(*p[0], s->screenWidth_);
        if (n > 1) s->screenHeight_ = ToInt(*p[1], s->screenHeight_);
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error makeCurrentCb(tTJSVariant *r, tjs_int, tTJSVariant **, GLESModule *) {
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error beginSceneCb(tTJSVariant *r, tjs_int, tTJSVariant **, GLESModule *s) {
        if (s) {
            tjs_int w = NormalizeExtent(s->screenWidth_, 1920);
            tjs_int h = NormalizeExtent(s->screenHeight_, 1080);
            s->fbo_.EnsureSize(static_cast<GLsizei>(w), static_cast<GLsizei>(h));
            s->fbo_.Bind();
            s->sceneActive_ = true;
        }
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error endSceneCb(tTJSVariant *r, tjs_int, tTJSVariant **, GLESModule *s) {
        if (s) {
            s->fbo_.Unbind();
            s->sceneActive_ = false;
        }
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error finalizeCb(tTJSVariant *r, tjs_int, tTJSVariant **, GLESModule *s) {
        if (s) s->fbo_.Destroy();
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error captureCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESModule *s) {
        tjs_int w = NormalizeExtent(s ? s->screenWidth_ : 0, 1920);
        tjs_int h = NormalizeExtent(s ? s->screenHeight_ : 0, 1080);
        InvokeCaptureCallback("GLESModule.capture", w, h, n, p);
        if (r) *r = (n > 0 && p) ? *p[0] : tTJSVariant(true);
        return TJS_S_OK;
    }

    static tjs_error glesCaptureCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESModule *s) {
        return captureCb(r, n, p, s);
    }

    static tjs_error captureScreenCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESModule *s) {
        return captureCb(r, n, p, s);
    }

    static tjs_error glesCaptureScreenCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESModule *s) {
        return captureCb(r, n, p, s);
    }

    static tjs_error copyLayerCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESModule *s) {
        iTJSDispatch2 *layer = FindLayerInParams(n, p);
        if (layer) {
            if (s && s->fbo_.GetFBO())
                CopyPixelsToLayer(s->fbo_, n, p);
            else if (g_live2dRenderTarget.fbo)
                CopyFBOToLayer(g_live2dRenderTarget.fbo,
                               g_live2dRenderTarget.width,
                               g_live2dRenderTarget.height, layer);
        }
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error glesCopyLayerCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESModule *s) {
        return copyLayerCb(r, n, p, s);
    }

    static tjs_error drawLayerCb(tTJSVariant *r, tjs_int, tTJSVariant **, GLESModule *) {
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error glesDrawLayerCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESModule *s) {
        return drawLayerCb(r, n, p, s);
    }

    static tjs_error drawAffineCb(tTJSVariant *r, tjs_int, tTJSVariant **, GLESModule *) {
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error drawAffineGLESCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESModule *s) {
        return drawAffineCb(r, n, p, s);
    }

    static tjs_error renderCb(tTJSVariant *r, tjs_int, tTJSVariant **, GLESModule *) {
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error setMatrixCb(tTJSVariant *r, tjs_int, tTJSVariant **, GLESModule *) {
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error createModelCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESModule *) {
        tTJSVariant model;
        tjs_error er = CreateObjectByExpression(&model, TJS_W("new Live2DModel()"), "createModel");
        if (TJS_FAILED(er)) { if (r) r->Clear(); return er; }
        InvokeLoadIfPresent(model, n, p, "createModel");
        if (r) *r = model;
        return TJS_S_OK;
    }

    static tjs_error createMatrixCb(tTJSVariant *r, tjs_int, tTJSVariant **, GLESModule *) {
        return CreateObjectByExpression(r, TJS_W("new Live2DMatrix()"), "createMatrix");
    }

    static tjs_error createDeviceCb(tTJSVariant *r, tjs_int, tTJSVariant **, GLESModule *) {
        return CreateObjectByExpression(r, TJS_W("new Live2DDevice()"), "createDevice");
    }

    tjs_int getScreenWidth() const { return screenWidth_; }
    void setScreenWidth(tjs_int v) { screenWidth_ = v; }
    tjs_int getScreenHeight() const { return screenHeight_; }
    void setScreenHeight(tjs_int v) { screenHeight_ = v; }

    bool isSceneActive() const { return sceneActive_; }
    OffscreenFBO &getFBO() { return fbo_; }

private:
    tjs_int screenWidth_ = 0;
    tjs_int screenHeight_ = 0;
    bool sceneActive_ = false;
    OffscreenFBO fbo_;
};

// ---------------------------------------------------------------------------
// Module object creation
// ---------------------------------------------------------------------------
static tjs_error CreateModuleObject(tTJSVariant *result, tjs_int w = 0, tjs_int h = 0) {
    auto *mod = new GLESModule();
    mod->setScreenWidth(w);
    mod->setScreenHeight(h);
    iTJSDispatch2 *obj = ncbInstanceAdaptor<GLESModule>::CreateAdaptor(mod);
    if (!obj) { delete mod; return CreateFallbackModuleObject(result, w, h); }
    SetResultObject(result, obj);
    obj->Release();
    return TJS_S_OK;
}

extern "C" tjs_error TVPKrkrGLESCreateModuleObject(tTJSVariant *result,
                                                    tjs_int w, tjs_int h) {
    return CreateModuleObject(result, w, h);
}

// ---------------------------------------------------------------------------
// DrawDevice getModule callback
// ---------------------------------------------------------------------------
static tjs_error DrawDeviceGetModuleCb(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                       iTJSDispatch2 *objthis) {
    static std::unordered_map<uintptr_t, std::unordered_map<ModuleName, tTJSVariant>> s_mod;
    const ModuleName mn = NormalizeModuleName(n, p);
    const uintptr_t key = reinterpret_cast<uintptr_t>(objthis);
    if (key) {
        auto dit = s_mod.find(key);
        if (dit != s_mod.end()) {
            auto mit = dit->second.find(mn);
            if (mit != dit->second.end() && mit->second.Type() == tvtObject &&
                mit->second.AsObjectNoAddRef())
            { if (r) *r = mit->second; return TJS_S_OK; }
        }
    }
    tTJSVariant created;
    tjs_error er = CreateModuleObject(&created);
    if (TJS_FAILED(er)) { if (r) r->Clear(); return er; }
    if (key && created.Type() == tvtObject && created.AsObjectNoAddRef())
        s_mod[key][mn] = created;
    if (r) *r = created;
    return TJS_S_OK;
}

// ---------------------------------------------------------------------------
// GLESAdaptor — per-window adaptor that proxies to the module
// ---------------------------------------------------------------------------
class GLESAdaptor {
public:
    GLESAdaptor() = default;

    GLESModule *FindModule() {
        if (cachedModule_) return cachedModule_;
        tTJSVariant mv;
        if (TJS_SUCCEEDED(GLESAdaptor::getModuleCb(&mv, 0, nullptr, this))) {
            iTJSDispatch2 *obj = mv.AsObjectNoAddRef();
            if (obj) cachedModule_ = ncbInstanceAdaptor<GLESModule>::GetNativeInstance(obj);
        }
        return cachedModule_;
    }

    static tjs_error getModuleCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
        static std::unordered_map<uintptr_t, std::unordered_map<ModuleName, tTJSVariant>> sm;
        const ModuleName mn = NormalizeModuleName(n, p);
        const uintptr_t key = reinterpret_cast<uintptr_t>(s);
        if (key) {
            auto dit = sm.find(key);
            if (dit != sm.end()) {
                auto mit = dit->second.find(mn);
                if (mit != dit->second.end() && mit->second.Type() == tvtObject &&
                    mit->second.AsObjectNoAddRef())
                { if (r) *r = mit->second; return TJS_S_OK; }
            }
        }
        tTJSVariant created;
        tjs_error er = s ? CreateModuleObject(&created, s->screenWidth_, s->screenHeight_)
                         : CreateModuleObject(&created);
        if (TJS_FAILED(er)) { if (r) r->Clear(); return er; }
        if (key && created.Type() == tvtObject && created.AsObjectNoAddRef())
            sm[key][mn] = created;
        if (r) *r = created;
        return TJS_S_OK;
    }

    static tjs_error setScreenSizeCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
        if (!s || !p) return TJS_S_OK;
        if (n > 0) s->screenWidth_ = ToInt(*p[0], s->screenWidth_);
        if (n > 1) s->screenHeight_ = ToInt(*p[1], s->screenHeight_);
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error makeCurrentCb(tTJSVariant *r, tjs_int, tTJSVariant **, GLESAdaptor *) {
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error beginSceneCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
        auto *mod = s ? s->FindModule() : nullptr;
        if (mod) return GLESModule::beginSceneCb(r, n, p, mod);
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error endSceneCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
        auto *mod = s ? s->FindModule() : nullptr;
        if (mod) return GLESModule::endSceneCb(r, n, p, mod);
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error entryUpdateObjectCb(tTJSVariant *r, tjs_int n,
                                         tTJSVariant **p, GLESAdaptor *) {
        if (n > 0 && p) {
            iTJSDispatch2 *layer = FindLayerInParams(n, p);
            if (layer) g_registeredLayer = layer;
        }
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error captureCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
        tjs_int w = NormalizeExtent(s ? s->screenWidth_ : 0, 1920);
        tjs_int h = NormalizeExtent(s ? s->screenHeight_ : 0, 1080);
        InvokeCaptureCallback("GLESAdaptor.capture", w, h, n, p);
        if (r) *r = (n > 0 && p) ? *p[0] : tTJSVariant(true);
        return TJS_S_OK;
    }

    static tjs_error glesCaptureCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
        return captureCb(r, n, p, s);
    }

    static tjs_error captureScreenCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
        return captureCb(r, n, p, s);
    }

    static tjs_error glesCaptureScreenCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
        return captureCb(r, n, p, s);
    }

    static tjs_error copyLayerCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
        auto *mod = s ? s->FindModule() : nullptr;
        if (mod) return GLESModule::copyLayerCb(r, n, p, mod);
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error glesCopyLayerCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
        return copyLayerCb(r, n, p, s);
    }

    static tjs_error drawLayerCb(tTJSVariant *r, tjs_int, tTJSVariant **, GLESAdaptor *) {
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error glesDrawLayerCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
        return drawLayerCb(r, n, p, s);
    }

    static tjs_error drawAffineCb(tTJSVariant *r, tjs_int, tTJSVariant **, GLESAdaptor *) {
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error drawAffineGLESCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
        return drawAffineCb(r, n, p, s);
    }

    static tjs_error renderCb(tTJSVariant *r, tjs_int, tTJSVariant **, GLESAdaptor *) {
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error setMatrixCb(tTJSVariant *r, tjs_int, tTJSVariant **, GLESAdaptor *) {
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error createModelCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *) {
        tTJSVariant model;
        tjs_error er = CreateObjectByExpression(&model, TJS_W("new Live2DModel()"), "GLESAdaptor.createModel");
        if (TJS_FAILED(er)) { if (r) r->Clear(); return er; }
        InvokeLoadIfPresent(model, n, p, "GLESAdaptor.createModel");
        if (r) *r = model;
        return TJS_S_OK;
    }

    static tjs_error createMatrixCb(tTJSVariant *r, tjs_int, tTJSVariant **, GLESAdaptor *) {
        return CreateObjectByExpression(r, TJS_W("new Live2DMatrix()"), "GLESAdaptor.createMatrix");
    }

    static tjs_error createDeviceCb(tTJSVariant *r, tjs_int, tTJSVariant **, GLESAdaptor *) {
        return CreateObjectByExpression(r, TJS_W("new Live2DDevice()"), "GLESAdaptor.createDevice");
    }

    static tjs_error finalizeCb(tTJSVariant *r, tjs_int, tTJSVariant **, GLESAdaptor *) {
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error glesEntryCb(tTJSVariant *r, tjs_int, tTJSVariant **, GLESAdaptor *) {
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error glesRemoveCb(tTJSVariant *r, tjs_int, tTJSVariant **, GLESAdaptor *) {
        if (r) *r = true; return TJS_S_OK;
    }

    tjs_int getScreenWidth() const { return screenWidth_; }
    void setScreenWidth(tjs_int v) { screenWidth_ = v; }
    tjs_int getScreenHeight() const { return screenHeight_; }
    void setScreenHeight(tjs_int v) { screenHeight_ = v; }

private:
    tjs_int screenWidth_ = 0;
    tjs_int screenHeight_ = 0;
    GLESModule *cachedModule_ = nullptr;
};

} // namespace

// ---------------------------------------------------------------------------
// NCB Registration
// ---------------------------------------------------------------------------
static void KrkrGlesPreRegist() {}
static void KrkrGlesPostRegist() {}
NCB_PRE_REGIST_CALLBACK(KrkrGlesPreRegist);
NCB_POST_REGIST_CALLBACK(KrkrGlesPostRegist);

NCB_REGISTER_CLASS(GLESModule) {
    Constructor();
    NCB_PROPERTY(screenWidth, getScreenWidth, setScreenWidth);
    NCB_PROPERTY(screenHeight, getScreenHeight, setScreenHeight);
    NCB_METHOD_RAW_CALLBACK(entryUpdateObject, &GLESModule::entryUpdateObjectCb, 0);
    NCB_METHOD_RAW_CALLBACK(setScreenSize, &GLESModule::setScreenSizeCb, 0);
    NCB_METHOD_RAW_CALLBACK(makeCurrent, &GLESModule::makeCurrentCb, 0);
    NCB_METHOD_RAW_CALLBACK(beginScene, &GLESModule::beginSceneCb, 0);
    NCB_METHOD_RAW_CALLBACK(endScene, &GLESModule::endSceneCb, 0);
    NCB_METHOD_RAW_CALLBACK(finalize, &GLESModule::finalizeCb, 0);
    NCB_METHOD_RAW_CALLBACK(capture, &GLESModule::captureCb, 0);
    NCB_METHOD_RAW_CALLBACK(captureScreen, &GLESModule::captureScreenCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCapture, &GLESModule::glesCaptureCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCaptureScreen, &GLESModule::glesCaptureScreenCb, 0);
    NCB_METHOD_RAW_CALLBACK(copyLayer, &GLESModule::copyLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCopyLayer, &GLESModule::glesCopyLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawLayer, &GLESModule::drawLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesDrawLayer, &GLESModule::glesDrawLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawAffine, &GLESModule::drawAffineCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawAffineGLES, &GLESModule::drawAffineGLESCb, 0);
    NCB_METHOD_RAW_CALLBACK(render, &GLESModule::renderCb, 0);
    NCB_METHOD_RAW_CALLBACK(setMatrix, &GLESModule::setMatrixCb, 0);
    NCB_METHOD_RAW_CALLBACK(createModel, &GLESModule::createModelCb, 0);
    NCB_METHOD_RAW_CALLBACK(createMatrix, &GLESModule::createMatrixCb, 0);
    NCB_METHOD_RAW_CALLBACK(createDevice, &GLESModule::createDeviceCb, 0);
}

NCB_REGISTER_CLASS(GLESAdaptor) {
    Constructor();
    NCB_PROPERTY(screenWidth, getScreenWidth, setScreenWidth);
    NCB_PROPERTY(screenHeight, getScreenHeight, setScreenHeight);
    NCB_METHOD_RAW_CALLBACK(getModule, &GLESAdaptor::getModuleCb, 0);
    NCB_METHOD_RAW_CALLBACK(setScreenSize, &GLESAdaptor::setScreenSizeCb, 0);
    NCB_METHOD_RAW_CALLBACK(makeCurrent, &GLESAdaptor::makeCurrentCb, 0);
    NCB_METHOD_RAW_CALLBACK(beginScene, &GLESAdaptor::beginSceneCb, 0);
    NCB_METHOD_RAW_CALLBACK(endScene, &GLESAdaptor::endSceneCb, 0);
    NCB_METHOD_RAW_CALLBACK(entryUpdateObject, &GLESAdaptor::entryUpdateObjectCb, 0);
    NCB_METHOD_RAW_CALLBACK(capture, &GLESAdaptor::captureCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCapture, &GLESAdaptor::glesCaptureCb, 0);
    NCB_METHOD_RAW_CALLBACK(captureScreen, &GLESAdaptor::captureScreenCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCaptureScreen, &GLESAdaptor::glesCaptureScreenCb, 0);
    NCB_METHOD_RAW_CALLBACK(copyLayer, &GLESAdaptor::copyLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCopyLayer, &GLESAdaptor::glesCopyLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawLayer, &GLESAdaptor::drawLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesDrawLayer, &GLESAdaptor::glesDrawLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawAffine, &GLESAdaptor::drawAffineCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawAffineGLES, &GLESAdaptor::drawAffineGLESCb, 0);
    NCB_METHOD_RAW_CALLBACK(render, &GLESAdaptor::renderCb, 0);
    NCB_METHOD_RAW_CALLBACK(setMatrix, &GLESAdaptor::setMatrixCb, 0);
    NCB_METHOD_RAW_CALLBACK(createModel, &GLESAdaptor::createModelCb, 0);
    NCB_METHOD_RAW_CALLBACK(createMatrix, &GLESAdaptor::createMatrixCb, 0);
    NCB_METHOD_RAW_CALLBACK(createDevice, &GLESAdaptor::createDeviceCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesEntry, &GLESAdaptor::glesEntryCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesRemove, &GLESAdaptor::glesRemoveCb, 0);
    NCB_METHOD_RAW_CALLBACK(finalize, &GLESAdaptor::finalizeCb, 0);
}

NCB_ATTACH_FUNCTION_WITHTAG(getModule, WindowPassThroughDrawDevice,
                            Window.PassThroughDrawDevice, DrawDeviceGetModuleCb);
NCB_ATTACH_FUNCTION_WITHTAG(getModule, WindowBasicDrawDevice,
                            Window.BasicDrawDevice, DrawDeviceGetModuleCb);

extern "C" void TVPRegisterKrkrGLESPluginAnchor() {}
