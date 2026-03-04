#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include <spdlog/spdlog.h>
#include "tjs.h"
#include "ncbind.hpp"
#include "StorageIntf.h"
#include "EventIntf.h"
#include "WindowIntf.h"

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <minizip/unzip.h>

// Cubism SDK
#include "CubismFramework.hpp"
#include "CubismModelSettingJson.hpp"
#include "Model/CubismMoc.hpp"
#include "Model/CubismModel.hpp"
#include "Model/CubismUserModel.hpp"
#include "Motion/CubismMotion.hpp"
#include "Motion/CubismMotionManager.hpp"
#include "Rendering/OpenGL/CubismRenderer_OpenGLES2.hpp"
#include "Math/CubismMatrix44.hpp"
#include "Id/CubismIdManager.hpp"

#define NCB_MODULE_NAME TJS_W("krkrlive2d.dll")

using namespace Live2D::Cubism::Framework;

// KTX texture loader is defined in krkrgles.cpp
extern "C" GLuint LoadKtxTexture(const uint8_t *data, size_t dataSize);

// Shared render target so krkrgles.cpp copyLayer can read from
// the Live2D model's internal FBO.
struct Live2DRenderTarget {
    GLuint fbo   = 0;
    GLsizei width  = 0;
    GLsizei height = 0;
};
Live2DRenderTarget g_live2dRenderTarget;

extern void TVPSetPostDrawHook(void (*hook)());

// Registered Layer from krkrgles.cpp — used to blit Live2D content
extern iTJSDispatch2 *KrkrGLES_GetRegisteredLayer();

// CopyFBOToLayer from krkrgles.cpp — auto-switches GPU/CPU path
extern bool CopyFBOToLayer(GLuint fbo, GLsizei srcW, GLsizei srcH,
                           iTJSDispatch2 *layer);

class CubismLive2DModel; // forward
static std::vector<CubismLive2DModel*> g_activeModels;
static void EnsureContinuousHook(); // forward

// ---------------------------------------------------------------------------
// Cubism Allocator — uses standard malloc/free
// ---------------------------------------------------------------------------
namespace {

class CubismAllocator : public ICubismAllocator {
public:
    void *Allocate(const csmSizeType size) override { return std::malloc(size); }
    void Deallocate(void *mem) override { std::free(mem); }
    void *AllocateAligned(const csmSizeType size, const csmUint32 alignment) override {
        size_t offset = alignment - 1 + sizeof(void *);
        void *raw = std::malloc(size + offset);
        if (!raw) return nullptr;
        void **aligned = reinterpret_cast<void **>(
            (reinterpret_cast<size_t>(raw) + offset) & ~(size_t(alignment) - 1));
        aligned[-1] = raw;
        return aligned;
    }
    void DeallocateAligned(void *mem) override {
        if (mem) std::free(static_cast<void **>(mem)[-1]);
    }
};

static CubismAllocator s_allocator;
static bool s_cubismInitialized = false;

void EnsureCubismInitialized() {
    if (s_cubismInitialized) return;
    CubismFramework::Option opt;
    opt.LogFunction = [](const char *msg) { spdlog::debug("Cubism: {}", msg); };
    opt.LoggingLevel = CubismFramework::Option::LogLevel_Warning;
    CubismFramework::StartUp(&s_allocator, &opt);
    CubismFramework::Initialize();
    s_cubismInitialized = true;
    spdlog::info("krkrlive2d: Cubism SDK initialized");
}

// ---------------------------------------------------------------------------
// ZIP helper — extract ALL entries from a ZIP archive in one pass.
// Writes ZIP to a temp file once, reads all entries, then deletes.
// ---------------------------------------------------------------------------
using ZipArchive = std::unordered_map<std::string, std::vector<uint8_t>>;

static bool ExtractZipToMemory(const uint8_t *zipData, size_t zipSize,
                               ZipArchive &out) {
    out.clear();
    char tmpPath[256];
    std::snprintf(tmpPath, sizeof(tmpPath), "/tmp/krkr_l2d_%p.zip",
                  static_cast<const void *>(zipData));
    {
        FILE *f = std::fopen(tmpPath, "wb");
        if (!f) return false;
        std::fwrite(zipData, 1, zipSize, f);
        std::fclose(f);
    }

    unzFile zf = unzOpen(tmpPath);
    if (!zf) { std::remove(tmpPath); return false; }

    int ret = unzGoToFirstFile(zf);
    while (ret == UNZ_OK) {
        char name[512];
        unz_file_info info;
        unzGetCurrentFileInfo(zf, &info, name, sizeof(name), nullptr, 0, nullptr, 0);
        if (info.uncompressed_size > 0 && unzOpenCurrentFile(zf) == UNZ_OK) {
            std::vector<uint8_t> buf(info.uncompressed_size);
            int bytesRead = unzReadCurrentFile(zf, buf.data(),
                                               static_cast<unsigned>(buf.size()));
            if (bytesRead == static_cast<int>(buf.size()))
                out[std::string(name)] = std::move(buf);
            unzCloseCurrentFile(zf);
        }
        ret = unzGoToNextFile(zf);
    }
    unzClose(zf);
    std::remove(tmpPath);
    return !out.empty();
}

// ---------------------------------------------------------------------------
// Load file from krkr2 storage system (XP3 / filesystem)
// ---------------------------------------------------------------------------
static bool LoadFromStorage(const ttstr &path, std::vector<uint8_t> &out) {
    try {
        tTJSBinaryStream *stream = TVPCreateStream(path, TJS_BS_READ);
        if (!stream) return false;
        tjs_uint64 size = stream->GetSize();
        out.resize(static_cast<size_t>(size));
        stream->ReadBuffer(out.data(), static_cast<tjs_uint>(size));
        delete stream;
        return true;
    } catch (...) {
        return false;
    }
}

// ---------------------------------------------------------------------------
// Helper conversions
// ---------------------------------------------------------------------------
inline ttstr ToTTStr(const tTJSVariant &v) {
    return (v.Type() == tvtVoid) ? ttstr() : ttstr(v);
}

inline std::string ToKey(const tTJSVariant &v) { return ToTTStr(v).AsStdString(); }

inline tjs_real ToReal(const tTJSVariant &v, tjs_real fb = 0.0) {
    switch (v.Type()) {
    case tvtInteger: case tvtReal: return static_cast<tjs_real>(v);
    default: return fb;
    }
}

inline tjs_int ToInt(const tTJSVariant &v, tjs_int fb = 0) {
    switch (v.Type()) {
    case tvtInteger: return static_cast<tjs_int>(v);
    case tvtReal:    return static_cast<tjs_int>(static_cast<tjs_real>(v));
    default: return fb;
    }
}

inline void SetResultObject(tTJSVariant *r, iTJSDispatch2 *o) {
    if (r && o) *r = tTJSVariant(o, o);
}

inline iTJSDispatch2 *CreateStringArray(const std::vector<ttstr> &items) {
    iTJSDispatch2 *arr = TJSCreateArrayObject();
    if (!arr) return nullptr;
    for (tjs_int i = 0; i < static_cast<tjs_int>(items.size()); ++i) {
        tTJSVariant v(items[static_cast<size_t>(i)]);
        arr->PropSetByNum(TJS_MEMBERENSURE, i, &v, arr);
    }
    return arr;
}

inline iTJSDispatch2 *CreateIdNameDict(const ttstr &id, const ttstr &name) {
    iTJSDispatch2 *dict = TJSCreateDictionaryObject();
    if (!dict) return nullptr;
    tTJSVariant vId(id), vName(name);
    dict->PropSet(TJS_MEMBERENSURE, TJS_W("id"), nullptr, &vId, dict);
    dict->PropSet(TJS_MEMBERENSURE, TJS_W("name"), nullptr, &vName, dict);
    return dict;
}

} // namespace

// ---------------------------------------------------------------------------
// CubismLive2DModel — wraps CubismUserModel for the engine
// ---------------------------------------------------------------------------
class CubismLive2DModel : public CubismUserModel {
public:
    CubismLive2DModel() : CubismUserModel() {
        g_activeModels.push_back(this);
    }
    ~CubismLive2DModel() override {
        g_activeModels.erase(
            std::remove(g_activeModels.begin(), g_activeModels.end(), this),
            g_activeModels.end());
        DestroyInternalFBO();
        ReleaseTextures();
    }

    bool LoadFromL2D(const std::vector<uint8_t> &zipData, const std::string &baseName) {
        EnsureCubismInitialized();
        baseName_ = baseName;

        ZipArchive archive;
        if (!ExtractZipToMemory(zipData.data(), zipData.size(), archive)) {
            spdlog::error("krkrlive2d: failed to extract ZIP for {}", baseName);
            return false;
        }

        // Find and parse model3.json
        std::string modelJsonName = baseName + ".model3.json";
        auto jsonIt = archive.find(modelJsonName);
        if (jsonIt == archive.end()) {
            spdlog::error("krkrlive2d: model3.json not found in {}", modelJsonName);
            return false;
        }

        setting_ = CSM_NEW CubismModelSettingJson(jsonIt->second.data(),
                                                  static_cast<csmSizeType>(jsonIt->second.size()));
        if (!setting_) return false;

        // Load .moc3
        std::string mocName;
        if (setting_->GetModelFileName()) {
            csmString s = setting_->GetModelFileName();
            mocName = std::string(s.GetRawString());
        }
        if (mocName.empty()) mocName = baseName + ".moc3";

        auto mocIt = archive.find(mocName);
        if (mocIt == archive.end()) {
            spdlog::error("krkrlive2d: moc3 not found: {}", mocName);
            return false;
        }

        LoadModel(mocIt->second.data(), static_cast<csmSizeType>(mocIt->second.size()));
        if (!_moc || !_model) {
            spdlog::error("krkrlive2d: failed to create model from moc");
            return false;
        }

        // Load textures
        csmInt32 texCount = setting_->GetTextureCount();
        for (csmInt32 i = 0; i < texCount; ++i) {
            std::string texPath;
            if (setting_->GetTextureFileName(i)) {
                csmString s = setting_->GetTextureFileName(i);
                texPath = std::string(s.GetRawString());
            }
            if (texPath.empty()) continue;

            std::string ktxPath = texPath;
            auto dotPos = ktxPath.rfind('.');
            if (dotPos != std::string::npos)
                ktxPath = ktxPath.substr(0, dotPos) + ".ktx";

            GLuint texId = 0;

            auto ktxIt = archive.find(ktxPath);
            if (ktxIt != archive.end()) {
                texId = LoadKtxTexture(ktxIt->second.data(), ktxIt->second.size());
                if (texId)
                    spdlog::debug("krkrlive2d: loaded KTX texture #{} ({})", i, ktxPath);
            }

            if (!texId) {
                auto texIt = archive.find(texPath);
                if (texIt != archive.end()) {
                    spdlog::warn("krkrlive2d: PNG texture loading not yet implemented: {}",
                                 texPath);
                }
            }

            if (!texId) {
                glGenTextures(1, &texId);
                glBindTexture(GL_TEXTURE_2D, texId);
                uint32_t white = 0xFFFFFFFF;
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0,
                             GL_RGBA, GL_UNSIGNED_BYTE, &white);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glBindTexture(GL_TEXTURE_2D, 0);
                spdlog::warn("krkrlive2d: using placeholder for texture #{}", i);
            }

            textureIds_.push_back(texId);
        }

        CreateRenderer();
        auto *renderer = GetRenderer<Rendering::CubismRenderer_OpenGLES2>();
        if (!renderer) {
            spdlog::error("krkrlive2d: failed to create renderer");
            return false;
        }
        for (int i = 0; i < static_cast<int>(textureIds_.size()); ++i)
            renderer->BindTexture(static_cast<csmUint32>(i),
                                  textureIds_[static_cast<size_t>(i)]);
        renderer->SetMvpMatrix(&projMatrix_);
        renderer->IsPremultipliedAlpha(false);

        csmInt32 eyeBlinkCount = setting_->GetEyeBlinkParameterCount();
        for (csmInt32 i = 0; i < eyeBlinkCount; ++i) {
            CubismIdHandle pid = setting_->GetEyeBlinkParameterId(i);
            if (pid) _eyeBlinkIds.PushBack(pid);
        }
        if (_eyeBlinkIds.GetSize() > 0) {
            _eyeBlink = CubismEyeBlink::Create(setting_);
        }
        csmInt32 lipSyncCount = setting_->GetLipSyncParameterCount();
        for (csmInt32 i = 0; i < lipSyncCount; ++i) {
            CubismIdHandle pid = setting_->GetLipSyncParameterId(i);
            if (pid) _lipSyncIds.PushBack(pid);
        }

        // Load motions
        csmInt32 groupCount = setting_->GetMotionGroupCount();
        for (csmInt32 g = 0; g < groupCount; ++g) {
            const csmChar *group = setting_->GetMotionGroupName(g);
            if (!group) continue;
            csmInt32 motionCount = setting_->GetMotionCount(group);
            for (csmInt32 m = 0; m < motionCount; ++m) {
                const csmChar *motionFile = setting_->GetMotionFileName(group, m);
                if (!motionFile) continue;
                std::string motionPath(motionFile);
                auto motionIt = archive.find(motionPath);
                if (motionIt != archive.end()) {
                    CubismMotion *motion = static_cast<CubismMotion *>(
                        CubismMotion::Create(motionIt->second.data(),
                                            static_cast<csmSizeType>(motionIt->second.size())));
                    if (motion) {
                        csmFloat32 fadeIn = setting_->GetMotionFadeInTimeValue(group, m);
                        csmFloat32 fadeOut = setting_->GetMotionFadeOutTimeValue(group, m);
                        if (fadeIn >= 0.f) motion->SetFadeInTime(fadeIn);
                        if (fadeOut >= 0.f) motion->SetFadeOutTime(fadeOut);
                        motion->SetEffectIds(_eyeBlinkIds, _lipSyncIds);
                        std::string key = std::string(group) + "_" + std::to_string(m);
                        motions_[key] = motion;
                    }
                }
            }
        }

        if (_motionManager && !motions_.empty()) {
            auto it = motions_.begin();
            _motionManager->StartMotionPriority(it->second, false, 1);
            spdlog::debug("krkrlive2d: auto-started motion '{}'", it->first);
        }

        loaded_ = true;
        EnsureContinuousHook();
        spdlog::info("krkrlive2d: model loaded: {} ({} parts, {} drawables, {} textures, {} motions) "
                     "canvas={:.2f}x{:.2f} canvasPx={}x{} ppu={:.0f}",
                     baseName,
                     GetModel()->GetPartCount(),
                     GetModel()->GetDrawableCount(),
                     texCount,
                     static_cast<int>(motions_.size()),
                     GetModel()->GetCanvasWidth(),
                     GetModel()->GetCanvasHeight(),
                     static_cast<int>(GetModel()->GetCanvasWidthPixel()),
                     static_cast<int>(GetModel()->GetCanvasHeightPixel()),
                     GetModel()->GetPixelsPerUnit());
        return true;
    }

    void ContinuousUpdate() {
        if (!loaded_ || !GetModel()) return;

        auto now = std::chrono::steady_clock::now();
        float dt = 0.016f;
        if (lastUpdateTime_.time_since_epoch().count() > 0) {
            dt = std::chrono::duration<float>(now - lastUpdateTime_).count();
            if (dt < 0.002f) return;
            if (dt > 0.1f) dt = 0.1f;
        }
        lastUpdateTime_ = now;

        GetModel()->LoadParameters();
        if (_motionManager && _motionManager->IsFinished() && !motions_.empty()) {
            auto it = motions_.begin();
            _motionManager->StartMotionPriority(it->second, false, 1);
        }
        if (_motionManager) _motionManager->UpdateMotion(GetModel(), dt);
        GetModel()->SaveParameters();
        if (_eyeBlink) _eyeBlink->UpdateParameters(GetModel(), dt);
        GetModel()->Update();

        GLint savedFBO = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &savedFBO);
        GLint savedVP[4];
        glGetIntegerv(GL_VIEWPORT, savedVP);

        GLsizei canvasW = static_cast<GLsizei>(GetModel()->GetCanvasWidthPixel());
        GLsizei canvasH = static_cast<GLsizei>(GetModel()->GetCanvasHeightPixel());
        if (canvasW <= 0) canvasW = 1920;
        if (canvasH <= 0) canvasH = 1080;
        EnsureInternalFBO(canvasW, canvasH);

        glBindFramebuffer(GL_FRAMEBUFFER, internalFbo_);
        glViewport(0, 0, fboW_, fboH_);
        glClearColor(0.f, 0.f, 0.f, 0.f);
        glClear(GL_COLOR_BUFFER_BIT);

        auto *renderer = GetRenderer<Rendering::CubismRenderer_OpenGLES2>();
        if (renderer) {
            UpdateProjection();
            renderer->SetMvpMatrix(&projMatrix_);
            renderer->DrawModel();
        }

        g_live2dRenderTarget = { internalFbo_, fboW_, fboH_ };

        glBindFramebuffer(GL_FRAMEBUFFER, savedFBO);
        glViewport(savedVP[0], savedVP[1], savedVP[2], savedVP[3]);
    }

    void BlitOverlay() {
        if (!loaded_ || !internalFbo_ || !fboTex_) return;
        GLint vp[4];
        glGetIntegerv(GL_VIEWPORT, vp);
        GLint currentFBO = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &currentFBO);
        BlitFBOToTarget(currentFBO, vp[0], vp[1], vp[2], vp[3]);
    }

    void UpdateAndDraw() {
        if (!loaded_ || !GetModel()) return;
        ContinuousUpdate();
    }

    void UpdateProjection() {
        CubismMatrix44 proj;
        proj.LoadIdentity();

        if (GetModel() && _modelMatrix) {
            float cw = GetModel()->GetCanvasWidth();
            float ch = GetModel()->GetCanvasHeight();
            if (cw > 0.f && ch > 0.f) {
                // _modelMatrix SetHeight(2.0) maps model to NDC height [-1,1],
                // width to [-cw/ch, cw/ch]. FBO aspect matches model aspect,
                // so scale X by ch/cw to fit width into [-1,1].
                // Y-flip for GL bottom-up → engine top-down.
                proj.Scale(ch / cw, -1.0f);
            }
            proj.MultiplyByMatrix(_modelMatrix);
        }

        projMatrix_ = proj;
    }

    void SetRenderSize(int w, int h) {
        renderWidth_ = w;
        renderHeight_ = h;
    }

    void StartMotionByIndex(const std::string &group, int index) {
        std::string key = group + "_" + std::to_string(index);
        auto it = motions_.find(key);
        if (it != motions_.end() && _motionManager) {
            _motionManager->StartMotionPriority(it->second, false, 2);
        }
    }

    bool IsLoaded() const { return loaded_; }

private:
    void ReleaseTextures() {
        for (auto texId : textureIds_) {
            if (texId) glDeleteTextures(1, &texId);
        }
        textureIds_.clear();
    }

    void EnsureInternalFBO(GLsizei w, GLsizei h) {
        if (w > 4096) w = 4096;
        if (h > 4096) h = 4096;
        if (internalFbo_ && fboW_ == w && fboH_ == h) return;
        DestroyInternalFBO();
        glGenFramebuffers(1, &internalFbo_);
        glGenTextures(1, &fboTex_);
        glBindTexture(GL_TEXTURE_2D, fboTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, internalFbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, fboTex_, 0);
        fboW_ = w;
        fboH_ = h;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        spdlog::debug("krkrlive2d: created internal FBO {}x{} fbo={} tex={}",
                      w, h, internalFbo_, fboTex_);
    }

    void DestroyInternalFBO() {
        if (blitProgram_) { glDeleteProgram(blitProgram_); blitProgram_ = 0; }
        if (fboTex_) { glDeleteTextures(1, &fboTex_); fboTex_ = 0; }
        if (internalFbo_) { glDeleteFramebuffers(1, &internalFbo_); internalFbo_ = 0; }
        fboW_ = fboH_ = 0;
    }

    void EnsureBlitProgram() {
        if (blitProgram_) return;
        const char *vs =
            "#version 100\n"
            "attribute vec2 a_pos;\n"
            "uniform vec2 u_scale;\n"
            "varying vec2 v_uv;\n"
            "void main() {\n"
            "  gl_Position = vec4(a_pos * u_scale, 0.0, 1.0);\n"
            "  v_uv = a_pos * 0.5 + 0.5;\n"
            "}\n";
        const char *fs =
            "#version 100\n"
            "precision mediump float;\n"
            "varying vec2 v_uv;\n"
            "uniform sampler2D u_tex;\n"
            "void main() { gl_FragColor = texture2D(u_tex, v_uv); }\n";
        GLuint vsh = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vsh, 1, &vs, nullptr);
        glCompileShader(vsh);
        GLuint fsh = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fsh, 1, &fs, nullptr);
        glCompileShader(fsh);
        blitProgram_ = glCreateProgram();
        glAttachShader(blitProgram_, vsh);
        glAttachShader(blitProgram_, fsh);
        glBindAttribLocation(blitProgram_, 0, "a_pos");
        glLinkProgram(blitProgram_);
        glDeleteShader(vsh);
        glDeleteShader(fsh);
        locPos_   = glGetAttribLocation(blitProgram_, "a_pos");
        locTex_   = glGetUniformLocation(blitProgram_, "u_tex");
        locScale_ = glGetUniformLocation(blitProgram_, "u_scale");
    }

    void BlitFBOToTarget(GLint targetFBO, GLint vpX, GLint vpY,
                         GLsizei vpW, GLsizei vpH) {
        EnsureBlitProgram();
        if (!blitProgram_ || !fboTex_) return;

        glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
        glViewport(vpX, vpY, vpW, vpH);

        glUseProgram(blitProgram_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, fboTex_);
        glUniform1i(locTex_, 0);

        float modelAspect = (fboH_ > 0) ? static_cast<float>(fboW_) / fboH_ : 1.78f;
        float vpAspect    = (vpH > 0)   ? static_cast<float>(vpW)  / vpH   : 1.78f;
        float sx = 1.0f, sy = 1.0f;
        if (modelAspect > vpAspect)
            sy = vpAspect / modelAspect;
        else if (modelAspect < vpAspect)
            sx = modelAspect / vpAspect;
        glUniform2f(locScale_, sx, sy);

        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);

        static const float quad[] = { -1,-1, 1,-1, -1,1, 1,1 };
        glEnableVertexAttribArray(locPos_);
        glVertexAttribPointer(locPos_, 2, GL_FLOAT, GL_FALSE, 0, quad);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisableVertexAttribArray(locPos_);

        glBindTexture(GL_TEXTURE_2D, 0);
        glUseProgram(0);
    }

    bool loaded_ = false;
    int renderWidth_ = 1920;
    int renderHeight_ = 1080;
    std::string baseName_;
    CubismModelSettingJson *setting_ = nullptr;
    std::vector<GLuint> textureIds_;
    CubismMatrix44 projMatrix_;
    std::unordered_map<std::string, ACubismMotion *> motions_;
    csmVector<const CubismId *> _eyeBlinkIds;
    csmVector<const CubismId *> _lipSyncIds;

    GLuint internalFbo_ = 0;
    GLuint fboTex_ = 0;
    GLsizei fboW_ = 0;
    GLsizei fboH_ = 0;
    GLuint blitProgram_ = 0;
    GLint locPos_ = 0, locTex_ = 0, locScale_ = 0;
    std::chrono::steady_clock::time_point lastUpdateTime_;
};

// ---------------------------------------------------------------------------
// Continuous animation driver — updates all active Live2D models every frame.
// When a registered Layer is available, blits via GPU/CPU CopyFBOToLayer.
// Otherwise falls back to PostDrawHook overlay blit.
// ---------------------------------------------------------------------------
class Live2DContinuousCallback : public tTVPContinuousEventCallbackIntf {
public:
    void OnContinuousCallback(tjs_uint64 /*tick*/) override {
        bool anyActive = false;
        iTJSDispatch2 *layer = KrkrGLES_GetRegisteredLayer();
        for (auto *m : g_activeModels) {
            if (m && m->IsLoaded()) {
                m->ContinuousUpdate();
                anyActive = true;
                if (layer && g_live2dRenderTarget.fbo) {
                    CopyFBOToLayer(g_live2dRenderTarget.fbo,
                                   g_live2dRenderTarget.width,
                                   g_live2dRenderTarget.height, layer);
                }
            }
        }
        if (anyActive && !layer && TVPGetWindowCount() > 0) {
            tTJSNI_Window *win = TVPGetWindowListAt(0);
            if (win) TVPPostWindowUpdate(win);
        }
    }
};

static Live2DContinuousCallback g_live2dContinuousCb;
static bool g_continuousHookRegistered = false;

static void Live2DPostDrawHook() {
    if (KrkrGLES_GetRegisteredLayer()) return;
    for (auto *m : g_activeModels) {
        if (m && m->IsLoaded()) {
            m->BlitOverlay();
        }
    }
}

static void EnsureContinuousHook() {
    if (g_continuousHookRegistered) return;
    TVPAddContinuousEventHook(&g_live2dContinuousCb);
    TVPSetPostDrawHook(Live2DPostDrawHook);
    g_continuousHookRegistered = true;
    spdlog::info("krkrlive2d: continuous animation hook registered");
}

// ---------------------------------------------------------------------------
// Live2DMatrix — simple 2x3 affine matrix (TJS interface)
// ---------------------------------------------------------------------------
class Live2DMatrix {
public:
    Live2DMatrix() = default;

    static tjs_error setMatrixCb(tTJSVariant *, tjs_int n, tTJSVariant **p,
                                 Live2DMatrix *s) {
        if (!s || !p) return TJS_S_OK;
        for (int i = 0; i < 6 && i < n; ++i)
            s->m_[i] = ToReal(*p[i], s->m_[i]);
        return TJS_S_OK;
    }

private:
    std::array<tjs_real, 6> m_ { 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 };
};

// ---------------------------------------------------------------------------
// Live2DDevice — rendering device wrapper
// ---------------------------------------------------------------------------
class Live2DDevice {
public:
    Live2DDevice() = default;
    void beginScene() { inScene_ = true; }
    void endScene() { inScene_ = false; }
    void onBeginScene() { beginScene(); }
    void onEndScene() { endScene(); }

    static tjs_error renderCb(tTJSVariant *r, tjs_int numparams, tTJSVariant **param,
                              Live2DDevice *) {
        if (numparams > 0 && param && param[0] && param[0]->Type() == tvtObject) {
            iTJSDispatch2 *obj = param[0]->AsObjectNoAddRef();
            if (obj) {
                tjs_uint hint = 0;
                obj->FuncCall(0, TJS_W("sync"), &hint, nullptr, 0, nullptr, obj);
            }
        }
        if (r) *r = true;
        return TJS_S_OK;
    }

private:
    bool inScene_ = false;
};

// ---------------------------------------------------------------------------
// Live2DModel — TJS class wrapping CubismLive2DModel
// ---------------------------------------------------------------------------
class Live2DModel {
public:
    Live2DModel() { ensureDeviceObject(); }

    ~Live2DModel() {
        if (cubismModel_) { CSM_DELETE(cubismModel_); cubismModel_ = nullptr; }
        if (deviceObj_) { deviceObj_->Release(); deviceObj_ = nullptr; }
    }

    // Non-copyable for simplicity with Cubism resources
    Live2DModel(const Live2DModel &) = delete;
    Live2DModel &operator=(const Live2DModel &) = delete;

    // --- Core callbacks ---

    static tjs_error loadCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, Live2DModel *s) {
        if (!s) return TJS_S_OK;
        if (n <= 0 || !p) { if (r) *r = false; return TJS_S_OK; }

        ttstr storagePath = ToTTStr(*p[0]);
        s->storage_ = storagePath;

        // Load the .l2d file from the engine's storage
        std::vector<uint8_t> zipData;
        if (!LoadFromStorage(storagePath, zipData)) {
            spdlog::error("krkrlive2d: failed to load from storage: {}",
                          storagePath.AsStdString());
            if (r) *r = false;
        return TJS_S_OK;
    }

        // Extract base name from filename (e.g., "ev_cg003_02s" from path)
        std::string fullPath = storagePath.AsStdString();
        std::string baseName;
        auto slash = fullPath.rfind('/');
        auto bslash = fullPath.rfind('\\');
        size_t start = 0;
        if (slash != std::string::npos) start = slash + 1;
        if (bslash != std::string::npos && bslash + 1 > start) start = bslash + 1;
        baseName = fullPath.substr(start);
        auto dot = baseName.rfind('.');
        if (dot != std::string::npos) baseName = baseName.substr(0, dot);

        EnsureCubismInitialized();

        if (s->cubismModel_) { CSM_DELETE(s->cubismModel_); s->cubismModel_ = nullptr; }
        s->cubismModel_ = CSM_NEW CubismLive2DModel();
        if (!s->cubismModel_->LoadFromL2D(zipData, baseName)) {
            spdlog::error("krkrlive2d: model load failed: {}", baseName);
            CSM_DELETE(s->cubismModel_);
            s->cubismModel_ = nullptr;
            if (r) *r = false;
        return TJS_S_OK;
    }

        s->loaded_ = true;
        s->progress_ = 1.0;
        if (r) *r = true;
        return TJS_S_OK;
    }

    static tjs_error renderCb(tTJSVariant *r, tjs_int, tTJSVariant **, Live2DModel *s) {
        if (s && s->cubismModel_ && s->cubismModel_->IsLoaded()) {
            s->cubismModel_->UpdateAndDraw();
        }
        if (s) s->progress_ = 1.0;
        if (r) *r = true;
        return TJS_S_OK;
    }

    static tjs_error showCb(tTJSVariant *r, tjs_int, tTJSVariant **, Live2DModel *) {
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error hideCb(tTJSVariant *r, tjs_int, tTJSVariant **, Live2DModel *) {
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error getDeviceCb(tTJSVariant *r, tjs_int, tTJSVariant **, Live2DModel *s) {
        if (!r) return TJS_S_OK;
        if (!s || !s->deviceObj_) { r->Clear(); return TJS_S_OK; }
        *r = tTJSVariant(s->deviceObj_, s->deviceObj_);
        return TJS_S_OK;
    }

    static tjs_error setDeviceCb(tTJSVariant *, tjs_int n, tTJSVariant **p, Live2DModel *s) {
        if (!s || !p || n <= 0 || !p[0]) return TJS_S_OK;
        iTJSDispatch2 *obj = (p[0]->Type() == tvtObject) ? p[0]->AsObjectNoAddRef() : nullptr;
        if (obj == s->deviceObj_) return TJS_S_OK;
        if (obj) obj->AddRef();
        if (s->deviceObj_) s->deviceObj_->Release();
        s->deviceObj_ = obj;
        if (!s->deviceObj_) s->ensureDeviceObject();
        return TJS_S_OK;
    }

    // --- Parameter / Part / Motion stubs that work with Cubism model ---

    static tjs_error setScaleCb(tTJSVariant *, tjs_int n, tTJSVariant **p, Live2DModel *s) {
        if (s && p && n > 0) s->scale_ = ToReal(*p[0], s->scale_);
        return TJS_S_OK;
    }

    static tjs_error getScaleCb(tTJSVariant *r, tjs_int, tTJSVariant **, Live2DModel *s) {
        if (r && s) *r = s->scale_;
        return TJS_S_OK;
    }

    static tjs_error setMatrixCb(tTJSVariant *, tjs_int n, tTJSVariant **p, Live2DModel *s) {
        if (!s || !p) return TJS_S_OK;
        for (int i = 0; i < 6 && i < n; ++i)
            s->matrix_[i] = ToReal(*p[i], s->matrix_[i]);
        return TJS_S_OK;
    }

    static tjs_error setVoiceValueCb(tTJSVariant *, tjs_int n, tTJSVariant **p, Live2DModel *s) {
        if (s && p && n > 0) s->voiceValue_ = ToReal(*p[0], s->voiceValue_);
        return TJS_S_OK;
    }

    static tjs_error setVoiceWeightCb(tTJSVariant *, tjs_int n, tTJSVariant **p, Live2DModel *s) {
        if (s && p && n > 0) s->voiceWeight_ = ToReal(*p[0], s->voiceWeight_);
        return TJS_S_OK;
    }

    static tjs_error setVoiceModeCb(tTJSVariant *, tjs_int n, tTJSVariant **p, Live2DModel *s) {
        if (s && p && n > 0) s->voiceMode_ = ToInt(*p[0], s->voiceMode_);
        return TJS_S_OK;
    }

    static tjs_error setBlinkingIntervalCb(tTJSVariant *, tjs_int n, tTJSVariant **p, Live2DModel *s) {
        if (s && p && n > 0) s->blinkingInterval_ = ToReal(*p[0], s->blinkingInterval_);
        return TJS_S_OK;
    }

    static tjs_error setBlinkingSettingsCb(tTJSVariant *, tjs_int n, tTJSVariant **p, Live2DModel *s) {
        if (!s || !p) return TJS_S_OK;
        if (n > 0) s->blinkingInterval_ = ToReal(*p[0], s->blinkingInterval_);
        if (n > 1) s->blinkingMode_ = ToInt(*p[1], s->blinkingMode_);
        return TJS_S_OK;
    }

    static tjs_error setBlinkingModeCb(tTJSVariant *, tjs_int n, tTJSVariant **p, Live2DModel *s) {
        if (s && p && n > 0) s->blinkingMode_ = ToInt(*p[0], s->blinkingMode_);
        return TJS_S_OK;
    }

    static tjs_error setMosaicParamCb(tTJSVariant *, tjs_int n, tTJSVariant **p, Live2DModel *s) {
        if (!s || !p) return TJS_S_OK;
        if (n > 0) s->mosaicX_ = ToReal(*p[0], s->mosaicX_);
        if (n > 1) s->mosaicY_ = ToReal(*p[1], s->mosaicY_);
        return TJS_S_OK;
    }

    static tjs_error getExpressionCountCb(tTJSVariant *r, tjs_int, tTJSVariant **, Live2DModel *s) {
        if (r) *r = static_cast<tjs_int>(s ? s->expressionNames_.size() : 0);
        return TJS_S_OK;
    }

    static tjs_error getExpressionNameCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, Live2DModel *s) {
        if (!r || !s) return TJS_S_OK;
        tjs_int idx = (n > 0 && p) ? ToInt(*p[0], 0) : 0;
        if (idx >= 0 && idx < static_cast<tjs_int>(s->expressionNames_.size()))
            *r = s->expressionNames_[static_cast<size_t>(idx)];
        else *r = ttstr();
        return TJS_S_OK;
    }

    static tjs_error setExpressionCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, Live2DModel *s) {
        if (s && n > 0 && p) s->expression_ = ToTTStr(*p[0]);
        if (r) *r = true;
            return TJS_S_OK;
        }

    static tjs_error getExpressionCb(tTJSVariant *r, tjs_int, tTJSVariant **, Live2DModel *s) {
        if (r && s) *r = s->expression_;
        return TJS_S_OK;
    }

    static tjs_error fixExpressionCb(tTJSVariant *r, tjs_int, tTJSVariant **, Live2DModel *) {
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error getMotionGroupCountCb(tTJSVariant *r, tjs_int, tTJSVariant **, Live2DModel *s) {
        if (r) *r = static_cast<tjs_int>(s ? s->motionGroupNames_.size() : 0);
        return TJS_S_OK;
    }

    static tjs_error getMotionGroupNameCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, Live2DModel *s) {
        if (!r || !s) return TJS_S_OK;
        tjs_int idx = (n > 0 && p) ? ToInt(*p[0], 0) : 0;
        *r = (idx >= 0 && idx < static_cast<tjs_int>(s->motionGroupNames_.size()))
             ? s->motionGroupNames_[static_cast<size_t>(idx)] : ttstr();
        return TJS_S_OK;
    }

    static tjs_error getMotionCountCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, Live2DModel *s) {
        if (!r || !s) return TJS_S_OK;
        ttstr group = (n > 0 && p) ? ToTTStr(*p[0]) : TJS_W("main");
        auto it = s->motionNames_.find(group.AsStdString());
        *r = (it == s->motionNames_.end()) ? 0 : static_cast<tjs_int>(it->second.size());
        return TJS_S_OK;
    }

    static tjs_error getMotionNameCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, Live2DModel *s) {
        if (!r || !s) return TJS_S_OK;
        ttstr group = (n > 0 && p) ? ToTTStr(*p[0]) : TJS_W("main");
        tjs_int idx = (n > 1 && p) ? ToInt(*p[1], 0) : 0;
        auto it = s->motionNames_.find(group.AsStdString());
        if (it != s->motionNames_.end() && idx >= 0 &&
            idx < static_cast<tjs_int>(it->second.size()))
            *r = it->second[static_cast<size_t>(idx)];
        else *r = ttstr();
        return TJS_S_OK;
    }

    static tjs_error startMotionCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, Live2DModel *s) {
        if (!s) return TJS_S_OK;
        s->playing_ = true;
        ttstr motion = (n > 0 && p) ? ToTTStr(*p[0]) : TJS_W("idle");
        if (motion.IsEmpty()) motion = TJS_W("idle");
        s->currentMotions_.clear();
        s->currentMotions_.push_back(motion);
        if (r) *r = true;
        return TJS_S_OK;
    }

    static tjs_error stopMotionCb(tTJSVariant *r, tjs_int, tTJSVariant **, Live2DModel *s) {
        if (s) { s->playing_ = false; s->currentMotions_.clear(); }
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error getCurrentMotionsCb(tTJSVariant *r, tjs_int, tTJSVariant **, Live2DModel *s) {
        if (!r || !s) return TJS_S_OK;
        iTJSDispatch2 *arr = CreateStringArray(s->currentMotions_);
        SetResultObject(r, arr);
        if (arr) arr->Release();
        return TJS_S_OK;
    }

    static tjs_error getParameterCountCb(tTJSVariant *r, tjs_int, tTJSVariant **, Live2DModel *s) {
        if (r) {
            if (s && s->cubismModel_ && s->cubismModel_->GetModel())
                *r = s->cubismModel_->GetModel()->GetParameterCount();
            else
                *r = static_cast<tjs_int>(s ? s->parameterNames_.size() : 0);
        }
        return TJS_S_OK;
    }

    static tjs_error getParameterInfoCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, Live2DModel *s) {
        if (!r || !s) return TJS_S_OK;
        tjs_int idx = (n > 0 && p) ? ToInt(*p[0], 0) : 0;
        if (s->cubismModel_ && s->cubismModel_->GetModel()) {
            auto *model = s->cubismModel_->GetModel();
            if (idx >= 0 && idx < model->GetParameterCount()) {
                const char *id = model->GetParameterId(idx)->GetString().GetRawString();
                ttstr tid(id);
                iTJSDispatch2 *dict = CreateIdNameDict(tid, tid);
                SetResultObject(r, dict);
                if (dict) dict->Release();
        return TJS_S_OK;
    }
        }
        r->Clear();
        return TJS_S_OK;
    }

    static tjs_error getParameterValueCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, Live2DModel *s) {
        if (!r || !s || n <= 0 || !p) return TJS_S_OK;
        if (s->cubismModel_ && s->cubismModel_->GetModel()) {
            std::string key = ToKey(*p[0]);
            auto *id = CubismFramework::GetIdManager()->GetId(key.c_str());
            csmInt32 idx = s->cubismModel_->GetModel()->GetParameterIndex(id);
            if (idx >= 0)
                *r = static_cast<tjs_real>(s->cubismModel_->GetModel()->GetParameterValue(idx));
            else
                *r = 0.0;
        } else {
            auto it = s->parameterValues_.find(ToKey(*p[0]));
            *r = (it == s->parameterValues_.end()) ? 0.0 : it->second;
        }
        return TJS_S_OK;
    }

    static tjs_error setParameterValueCb(tTJSVariant *, tjs_int n, tTJSVariant **p, Live2DModel *s) {
        if (!s || !p || n <= 1) return TJS_S_OK;
        if (s->cubismModel_ && s->cubismModel_->GetModel()) {
            std::string key = ToKey(*p[0]);
            auto *id = CubismFramework::GetIdManager()->GetId(key.c_str());
            csmInt32 idx = s->cubismModel_->GetModel()->GetParameterIndex(id);
            if (idx >= 0)
                s->cubismModel_->GetModel()->SetParameterValue(
                    idx, static_cast<csmFloat32>(ToReal(*p[1], 0.0)));
        }
        s->parameterValues_[ToKey(*p[0])] = ToReal(*p[1], 0.0);
            return TJS_S_OK;
        }

    static tjs_error setParameterTypeCb(tTJSVariant *, tjs_int n, tTJSVariant **p, Live2DModel *s) {
        if (s && p && n > 1) s->parameterTypes_[ToKey(*p[0])] = ToInt(*p[1], 0);
        return TJS_S_OK;
    }

    static tjs_error setDiffParameterValueCb(tTJSVariant *, tjs_int n, tTJSVariant **p, Live2DModel *s) {
        if (s && p && n > 1) s->diffParameterValues_[ToKey(*p[0])] = ToReal(*p[1], 0.0);
        return TJS_S_OK;
    }

    static tjs_error getDiffParameterValueCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, Live2DModel *s) {
        if (!r || !s || !p || n <= 0) return TJS_S_OK;
        auto it = s->diffParameterValues_.find(ToKey(*p[0]));
        *r = (it == s->diffParameterValues_.end()) ? 0.0 : it->second;
        return TJS_S_OK;
    }

    static tjs_error addEyeBlinkIdCb(tTJSVariant *r, tjs_int, tTJSVariant **, Live2DModel *) {
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error addLipSyncIdCb(tTJSVariant *r, tjs_int, tTJSVariant **, Live2DModel *) {
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error canSyncCb(tTJSVariant *r, tjs_int, tTJSVariant **, Live2DModel *s) {
        if (r) *r = (s != nullptr); return TJS_S_OK;
    }

    static tjs_error syncCb(tTJSVariant *r, tjs_int, tTJSVariant **, Live2DModel *s) {
        if (s) s->progress_ = 1.0;
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error progressCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, Live2DModel *s) {
        if (s && p && n > 0) s->progress_ = ToReal(*p[0], s->progress_);
        if (r && s) *r = s->progress_;
        return TJS_S_OK;
    }

    static tjs_error cloneCb(tTJSVariant *r, tjs_int, tTJSVariant **, Live2DModel *) {
        // Clone not supported for Cubism models (shared GPU resources)
        if (r) r->Clear();
        return TJS_S_OK;
    }

    static tjs_error isPlayingCb(tTJSVariant *r, tjs_int, tTJSVariant **, Live2DModel *s) {
        if (r && s) *r = s->playing_;
        return TJS_S_OK;
    }

    static tjs_error getPartCountCb(tTJSVariant *r, tjs_int, tTJSVariant **, Live2DModel *s) {
        if (r) {
            if (s && s->cubismModel_ && s->cubismModel_->GetModel())
                *r = s->cubismModel_->GetModel()->GetPartCount();
            else
                *r = static_cast<tjs_int>(s ? s->partNames_.size() : 0);
        }
        return TJS_S_OK;
    }

    static tjs_error getPartInfoCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, Live2DModel *s) {
        if (!r || !s) return TJS_S_OK;
        tjs_int idx = (n > 0 && p) ? ToInt(*p[0], 0) : 0;
        if (s->cubismModel_ && s->cubismModel_->GetModel()) {
            auto *model = s->cubismModel_->GetModel();
            if (idx >= 0 && idx < model->GetPartCount()) {
                const char *id = model->GetPartId(idx)->GetString().GetRawString();
                ttstr tid(id);
                iTJSDispatch2 *dict = CreateIdNameDict(tid, tid);
                SetResultObject(r, dict);
                if (dict) dict->Release();
        return TJS_S_OK;
    }
        }
        r->Clear();
        return TJS_S_OK;
    }

    static tjs_error getPartValueCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, Live2DModel *s) {
        if (!r || !s || !p || n <= 0) return TJS_S_OK;
        if (s->cubismModel_ && s->cubismModel_->GetModel()) {
            auto *id = CubismFramework::GetIdManager()->GetId(ToKey(*p[0]).c_str());
            csmInt32 idx = s->cubismModel_->GetModel()->GetPartIndex(id);
            if (idx >= 0) {
                *r = static_cast<tjs_real>(s->cubismModel_->GetModel()->GetPartOpacity(idx));
                return TJS_S_OK;
            }
        }
        auto it = s->partValues_.find(ToKey(*p[0]));
        *r = (it == s->partValues_.end()) ? 1.0 : it->second;
        return TJS_S_OK;
    }

    static tjs_error setPartValueCb(tTJSVariant *, tjs_int n, tTJSVariant **p, Live2DModel *s) {
        if (!s || !p || n <= 1) return TJS_S_OK;
        if (s->cubismModel_ && s->cubismModel_->GetModel()) {
            auto *id = CubismFramework::GetIdManager()->GetId(ToKey(*p[0]).c_str());
            csmInt32 idx = s->cubismModel_->GetModel()->GetPartIndex(id);
            if (idx >= 0)
                s->cubismModel_->GetModel()->SetPartOpacity(
                    idx, static_cast<csmFloat32>(ToReal(*p[1], 1.0)));
        }
        s->partValues_[ToKey(*p[0])] = ToReal(*p[1], 1.0);
        return TJS_S_OK;
    }

    static tjs_error setPartFadeTimeCb(tTJSVariant *, tjs_int n, tTJSVariant **p, Live2DModel *s) {
        if (s && p && n > 0) s->partFadeTime_ = ToReal(*p[0], s->partFadeTime_);
        return TJS_S_OK;
    }

    static tjs_error getEventCountCb(tTJSVariant *r, tjs_int, tTJSVariant **, Live2DModel *) {
        if (r) *r = 0; return TJS_S_OK;
    }

    static tjs_error getEventNameCb(tTJSVariant *r, tjs_int, tTJSVariant **, Live2DModel *) {
        if (r) *r = ttstr(); return TJS_S_OK;
    }

    static tjs_error addVriableMotionCb(tTJSVariant *r, tjs_int, tTJSVariant **, Live2DModel *) {
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error delVariableMotionCb(tTJSVariant *r, tjs_int, tTJSVariant **, Live2DModel *) {
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error getVariableMotionCountCb(tTJSVariant *r, tjs_int, tTJSVariant **, Live2DModel *) {
        if (r) *r = 0; return TJS_S_OK;
    }

    static tjs_error getVariableMotionNameCb(tTJSVariant *r, tjs_int, tTJSVariant **, Live2DModel *) {
        if (r) *r = ttstr(); return TJS_S_OK;
    }

    static tjs_error getVariableMotionInfoCb(tTJSVariant *r, tjs_int, tTJSVariant **, Live2DModel *) {
        if (r) r->Clear(); return TJS_S_OK;
    }

    static tjs_error getVariableCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, Live2DModel *s) {
        if (!r || !s || !p || n <= 0) return TJS_S_OK;
        auto it = s->variables_.find(ToKey(*p[0]));
        if (it == s->variables_.end()) r->Clear();
        else *r = it->second;
        return TJS_S_OK;
    }

    static tjs_error setVariableCb(tTJSVariant *, tjs_int n, tTJSVariant **p, Live2DModel *s) {
        if (s && p && n > 1) s->variables_[ToKey(*p[0])] = *p[1];
        return TJS_S_OK;
    }

    static tjs_error isMosaicModelCb(tTJSVariant *r, tjs_int, tTJSVariant **, Live2DModel *) {
        if (r) *r = false; return TJS_S_OK;
    }

    static tjs_error reloadCb(tTJSVariant *r, tjs_int, tTJSVariant **, Live2DModel *) {
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error resetPartsCb(tTJSVariant *r, tjs_int, tTJSVariant **, Live2DModel *s) {
        if (s) s->partValues_.clear();
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error resetVariablesCb(tTJSVariant *r, tjs_int, tTJSVariant **, Live2DModel *s) {
        if (s) s->variables_.clear();
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error resetExpressionVariablesCb(tTJSVariant *r, tjs_int, tTJSVariant **, Live2DModel *s) {
        if (s) s->expression_ = ttstr();
        if (r) *r = true; return TJS_S_OK;
    }

private:
    void ensureDeviceObject() {
        if (deviceObj_) return;
        auto *dev = ncbInstanceAdaptor<Live2DDevice>::CreateAdaptor(new Live2DDevice());
        if (dev) deviceObj_ = dev;
    }

    bool loaded_ = false;
    bool playing_ = false;
    iTJSDispatch2 *deviceObj_ = nullptr;
    CubismLive2DModel *cubismModel_ = nullptr;

    ttstr storage_;
    ttstr expression_;
    tjs_real scale_ = 1.0;
    std::array<tjs_real, 6> matrix_ { 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 };
    tjs_real voiceValue_ = 0.0, voiceWeight_ = 1.0;
    tjs_int voiceMode_ = 0;
    tjs_real blinkingInterval_ = 0.0;
    tjs_int blinkingMode_ = 0;
    tjs_real mosaicX_ = 0.0, mosaicY_ = 0.0;
    tjs_real partFadeTime_ = 0.0;
    tjs_real progress_ = 0.0;
    tjs_int renderWidth_ = 1920, renderHeight_ = 1080;

    std::vector<ttstr> expressionNames_{ TJS_W("default") };
    std::vector<ttstr> motionGroupNames_{ TJS_W("main") };
    std::unordered_map<std::string, std::vector<ttstr>> motionNames_;
    std::vector<ttstr> parameterNames_;
    std::vector<ttstr> partNames_{ TJS_W("PartMain") };
    std::vector<ttstr> currentMotions_;
    std::unordered_map<std::string, tjs_real> parameterValues_;
    std::unordered_map<std::string, tjs_real> diffParameterValues_;
    std::unordered_map<std::string, tjs_real> partValues_;
    std::unordered_map<std::string, tjs_int> parameterTypes_;
    std::unordered_map<std::string, tTJSVariant> variables_;
};

// ---------------------------------------------------------------------------
// NCB Registration
// ---------------------------------------------------------------------------
NCB_REGISTER_CLASS(Live2DMatrix) {
    Constructor();
    NCB_METHOD_RAW_CALLBACK(setMatrix, &Live2DMatrix::setMatrixCb, 0);
}

NCB_REGISTER_CLASS(Live2DDevice) {
    Constructor();
    NCB_METHOD(beginScene);
    NCB_METHOD(endScene);
    NCB_METHOD(onBeginScene);
    NCB_METHOD(onEndScene);
    NCB_METHOD_RAW_CALLBACK(render, &Live2DDevice::renderCb, 0);
}

NCB_REGISTER_CLASS(Live2DModel) {
    Constructor();
    NCB_PROPERTY_RAW_CALLBACK(device, Live2DModel::getDeviceCb, Live2DModel::setDeviceCb, 0);
    NCB_METHOD_RAW_CALLBACK(render, &Live2DModel::renderCb, 0);
    NCB_METHOD_RAW_CALLBACK(show, &Live2DModel::showCb, 0);
    NCB_METHOD_RAW_CALLBACK(hide, &Live2DModel::hideCb, 0);
    NCB_METHOD_RAW_CALLBACK(progress, &Live2DModel::progressCb, 0);
    NCB_METHOD_RAW_CALLBACK(load, &Live2DModel::loadCb, 0);
    NCB_METHOD_RAW_CALLBACK(clone, &Live2DModel::cloneCb, 0);
    NCB_METHOD_RAW_CALLBACK(setScale, &Live2DModel::setScaleCb, 0);
    NCB_METHOD_RAW_CALLBACK(getScale, &Live2DModel::getScaleCb, 0);
    NCB_METHOD_RAW_CALLBACK(setMatrix, &Live2DModel::setMatrixCb, 0);
    NCB_METHOD_RAW_CALLBACK(setVoiceValue, &Live2DModel::setVoiceValueCb, 0);
    NCB_METHOD_RAW_CALLBACK(setVoiceWeight, &Live2DModel::setVoiceWeightCb, 0);
    NCB_METHOD_RAW_CALLBACK(setVoiceMode, &Live2DModel::setVoiceModeCb, 0);
    NCB_METHOD_RAW_CALLBACK(setBlinkingInterval, &Live2DModel::setBlinkingIntervalCb, 0);
    NCB_METHOD_RAW_CALLBACK(setBlinkingSettings, &Live2DModel::setBlinkingSettingsCb, 0);
    NCB_METHOD_RAW_CALLBACK(setBlinkingMode, &Live2DModel::setBlinkingModeCb, 0);
    NCB_METHOD_RAW_CALLBACK(setMosaicParam, &Live2DModel::setMosaicParamCb, 0);
    NCB_METHOD_RAW_CALLBACK(getExpressionCount, &Live2DModel::getExpressionCountCb, 0);
    NCB_METHOD_RAW_CALLBACK(getExpressionName, &Live2DModel::getExpressionNameCb, 0);
    NCB_METHOD_RAW_CALLBACK(setExpression, &Live2DModel::setExpressionCb, 0);
    NCB_METHOD_RAW_CALLBACK(getExpression, &Live2DModel::getExpressionCb, 0);
    NCB_METHOD_RAW_CALLBACK(fixExpression, &Live2DModel::fixExpressionCb, 0);
    NCB_METHOD_RAW_CALLBACK(getMotionGroupCount, &Live2DModel::getMotionGroupCountCb, 0);
    NCB_METHOD_RAW_CALLBACK(getMotionGroupName, &Live2DModel::getMotionGroupNameCb, 0);
    NCB_METHOD_RAW_CALLBACK(getMotionCount, &Live2DModel::getMotionCountCb, 0);
    NCB_METHOD_RAW_CALLBACK(getMotionName, &Live2DModel::getMotionNameCb, 0);
    NCB_METHOD_RAW_CALLBACK(startMotion, &Live2DModel::startMotionCb, 0);
    NCB_METHOD_RAW_CALLBACK(stopMotion, &Live2DModel::stopMotionCb, 0);
    NCB_METHOD_RAW_CALLBACK(getCurrentMotions, &Live2DModel::getCurrentMotionsCb, 0);
    NCB_METHOD_RAW_CALLBACK(isPlaying, &Live2DModel::isPlayingCb, 0);
    NCB_METHOD_RAW_CALLBACK(getParameterCount, &Live2DModel::getParameterCountCb, 0);
    NCB_METHOD_RAW_CALLBACK(getParameterInfo, &Live2DModel::getParameterInfoCb, 0);
    NCB_METHOD_RAW_CALLBACK(getParameterValue, &Live2DModel::getParameterValueCb, 0);
    NCB_METHOD_RAW_CALLBACK(setParameterValue, &Live2DModel::setParameterValueCb, 0);
    NCB_METHOD_RAW_CALLBACK(setParameterType, &Live2DModel::setParameterTypeCb, 0);
    NCB_METHOD_RAW_CALLBACK(setDiffParameterValue, &Live2DModel::setDiffParameterValueCb, 0);
    NCB_METHOD_RAW_CALLBACK(getDiffParameterValue, &Live2DModel::getDiffParameterValueCb, 0);
    NCB_METHOD_RAW_CALLBACK(addEyeBlinkId, &Live2DModel::addEyeBlinkIdCb, 0);
    NCB_METHOD_RAW_CALLBACK(addLipSyncId, &Live2DModel::addLipSyncIdCb, 0);
    NCB_METHOD_RAW_CALLBACK(canSync, &Live2DModel::canSyncCb, 0);
    NCB_METHOD_RAW_CALLBACK(sync, &Live2DModel::syncCb, 0);
    NCB_METHOD_RAW_CALLBACK(getPartCount, &Live2DModel::getPartCountCb, 0);
    NCB_METHOD_RAW_CALLBACK(getPartInfo, &Live2DModel::getPartInfoCb, 0);
    NCB_METHOD_RAW_CALLBACK(setPart, &Live2DModel::setPartValueCb, 0);
    NCB_METHOD_RAW_CALLBACK(getPartValue, &Live2DModel::getPartValueCb, 0);
    NCB_METHOD_RAW_CALLBACK(setPartValue, &Live2DModel::setPartValueCb, 0);
    NCB_METHOD_RAW_CALLBACK(setPartFadeTime, &Live2DModel::setPartFadeTimeCb, 0);
    NCB_METHOD_RAW_CALLBACK(getEventCount, &Live2DModel::getEventCountCb, 0);
    NCB_METHOD_RAW_CALLBACK(getEventName, &Live2DModel::getEventNameCb, 0);
    NCB_METHOD_RAW_CALLBACK(addVriableMotion, &Live2DModel::addVriableMotionCb, 0);
    NCB_METHOD_RAW_CALLBACK(delVariableMotion, &Live2DModel::delVariableMotionCb, 0);
    NCB_METHOD_RAW_CALLBACK(getVariableMotionCount, &Live2DModel::getVariableMotionCountCb, 0);
    NCB_METHOD_RAW_CALLBACK(getVariableMotionName, &Live2DModel::getVariableMotionNameCb, 0);
    NCB_METHOD_RAW_CALLBACK(getVariableMotionInfo, &Live2DModel::getVariableMotionInfoCb, 0);
    NCB_METHOD_RAW_CALLBACK(getVariable, &Live2DModel::getVariableCb, 0);
    NCB_METHOD_RAW_CALLBACK(setVariable, &Live2DModel::setVariableCb, 0);
    NCB_METHOD_RAW_CALLBACK(isMosaicModel, &Live2DModel::isMosaicModelCb, 0);
    NCB_METHOD_RAW_CALLBACK(reload, &Live2DModel::reloadCb, 0);
    NCB_METHOD_RAW_CALLBACK(resetParts, &Live2DModel::resetPartsCb, 0);
    NCB_METHOD_RAW_CALLBACK(resetVariables, &Live2DModel::resetVariablesCb, 0);
    NCB_METHOD_RAW_CALLBACK(resetExpressionVariables, &Live2DModel::resetExpressionVariablesCb, 0);
}

extern "C" void TVPRegisterKrkrLive2DPluginAnchor() {}
