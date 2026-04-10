/**
 * @file ui_stubs.cpp
 * @brief Stub implementations for UI layer functions and platform
 *        functions that were previously provided by MainScene.cpp,
 *        AppDelegate.cpp, and the environ/ui/ directory.
 *
 * With the migration to Flutter-based UI, all of these are replaced by
 * minimal stubs that either log a warning or return a sensible default.
 *
 * Functions stubbed here are called from the engine core and must link,
 * but their functionality will be provided by the Flutter host layer.
 */

#include <spdlog/spdlog.h>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>

#include "tjsCommHead.h"
#include "tjsConfig.h"
#include "WindowIntf.h"
#include "WindowImpl.h"
#include "MenuItemIntf.h"
#include "Platform.h"
#include "TVPWindow.h"
#include "Application.h"
#include "RenderManager.h"
#include "krkr_egl_context.h"
#include "SysInitImpl.h"

#include <GLES3/gl3.h>

// ---------------------------------------------------------------------------
// Live2D post-draw hook — called after scene blit in UpdateDrawBuffer
// ---------------------------------------------------------------------------
static void (*g_postDrawHook)() = nullptr;
void TVPSetPostDrawHook(void (*hook)()) { g_postDrawHook = hook; }

// ---------------------------------------------------------------------------
// FlutterWindowLayer — concrete iWindowLayer for Flutter host mode.
// Provides a logical window backed by the ANGLE EGL Pbuffer surface.
// Rendering output goes through glReadPixels in the engine_api layer.
// ---------------------------------------------------------------------------
class FlutterWindowLayer : public iWindowLayer {
public:
    explicit FlutterWindowLayer(tTJSNI_Window *owner)
        : owner_(owner), visible_(true), caption_("krkr2"),
          width_(0), height_(0), active_(true), closing_(false) {
        // Get initial size from EGL context
        auto& egl = krkr::GetEngineEGLContext();
        if (egl.IsValid()) {
            width_  = static_cast<tjs_int>(egl.GetWidth());
            height_ = static_cast<tjs_int>(egl.GetHeight());
        } else {
            width_  = 1280;
            height_ = 720;
        }
        spdlog::info("FlutterWindowLayer created: {}x{}", width_, height_);
    }

    ~FlutterWindowLayer() {
        if(blit_program_) {
            glDeleteProgram(blit_program_);
            blit_program_ = 0;
        }
        if(blit_vbo_) {
            glDeleteBuffers(1, &blit_vbo_);
            blit_vbo_ = 0;
        }
        if(blit_texture_) {
            glDeleteTextures(1, &blit_texture_);
            blit_texture_ = 0;
        }
        spdlog::debug("FlutterWindowLayer destroyed");
    }

    // -- Pure virtual implementations --

    void SetPaintBoxSize(tjs_int w, tjs_int h) override {
        // Only set WindowSize here — DestRect is exclusively managed by
        // UpdateDrawBuffer() which knows the correct letterbox viewport.
        // Setting DestRect here would overwrite the viewport offset and
        // cause mouse Y-axis misalignment.
        if (!owner_) return;
        auto* dd = owner_->GetDrawDevice();
        if (!dd) return;

        width_ = w;
        height_ = h;

        auto& egl = krkr::GetEngineEGLContext();
        tjs_int surf_w = egl.IsValid() ? static_cast<tjs_int>(egl.GetWidth())  : w;
        tjs_int surf_h = egl.IsValid() ? static_cast<tjs_int>(egl.GetHeight()) : h;
        if (surf_w <= 0) surf_w = w;
        if (surf_h <= 0) surf_h = h;

        dd->SetWindowSize(surf_w, surf_h);
        spdlog::debug("FlutterWindowLayer::SetPaintBoxSize: layer={}x{}, surface={}x{}",
                      w, h, surf_w, surf_h);
    }

    bool GetFormEnabled() override { return !closing_; }

    void SetDefaultMouseCursor() override {}

    void GetCursorPos(tjs_int &x, tjs_int &y) override {
        x = last_mouse_x_;
        y = last_mouse_y_;
    }

    void SetCursorPos(tjs_int x, tjs_int y) override {
        last_mouse_x_ = x;
        last_mouse_y_ = y;
    }

    void UpdateCursorPos(tjs_int x, tjs_int y) override {
        last_mouse_x_ = x;
        last_mouse_y_ = y;
    }

    void SetHintText(const ttstr &text) override {}

    void SetAttentionPoint(tjs_int left, tjs_int top,
                           const struct tTVPFont *font) override {}

    void ZoomRectangle(tjs_int &left, tjs_int &top, tjs_int &right,
                       tjs_int &bottom) override {
        // No zoom transformation — coordinates pass through 1:1
    }

    void BringToFront() override {}

    void ShowWindowAsModal() override {
        spdlog::warn("FlutterWindowLayer::ShowWindowAsModal: stub");
    }

    bool GetVisible() override { return visible_; }

    void SetVisible(bool bVisible) override { visible_ = bVisible; }

    const char *GetCaption() override { return caption_.c_str(); }

    void SetCaption(const std::string &cap) override { caption_ = cap; }

    void SetWidth(tjs_int w) override { width_ = w; }

    void SetHeight(tjs_int h) override { height_ = h; }

    void SetSize(tjs_int w, tjs_int h) override {
        width_ = w;
        height_ = h;
    }

    void GetSize(tjs_int &w, tjs_int &h) override {
        w = width_;
        h = height_;
    }

    [[nodiscard]] tjs_int GetWidth() const override { return width_; }

    [[nodiscard]] tjs_int GetHeight() const override { return height_; }

    void GetWinSize(tjs_int &w, tjs_int &h) override {
        w = width_;
        h = height_;
    }

    void SetZoom(tjs_int numer, tjs_int denom) override {
        ZoomNumer = numer;
        ZoomDenom = denom;
    }

    void UpdateDrawBuffer(iTVPTexture2D *tex) override {
        // Blit the composited scene texture to the render target.
        // When an IOSurface is attached, this goes directly to the shared
        // IOSurface (zero-copy to Flutter). Otherwise, falls back to
        // the EGL Pbuffer for glReadPixels-based retrieval.
        if (!tex) return;

        const tjs_uint tw = tex->GetWidth();
        const tjs_uint th = tex->GetHeight();
        if (tw == 0 || th == 0) return;

        EnsureBlitResources();

        auto& egl = krkr::GetEngineEGLContext();

        // ── Phase 1: Prepare the blit source texture ──────────────
        // This MUST happen BEFORE BindRenderTarget(), because
        // GetScanLineForRead() internally calls TVPSetRenderTarget()
        // which changes the FBO binding. We need the engine's FBO
        // to be active for reading pixels, then switch to IOSurface
        // FBO for the actual blit.
        const uint32_t nativeGLTex = tex->GetNativeGLTextureId();
        GLuint blitSrcTexture;

        if (nativeGLTex != 0) {
            // GPU fast-path: the composited scene is already in a GL texture.
            // We must detach it from the engine's FBO first to avoid
            // sampling a texture that is still an FBO attachment.
            // TVPSetRenderTarget(0) will unbind any texture from the engine FBO.
            extern void TVPSetRenderTarget(GLuint);

            TVPSetRenderTarget(0);
            blitSrcTexture = static_cast<GLuint>(nativeGLTex);
        } else {
            // CPU fallback: read pixel data and upload to our blit texture.
            blitSrcTexture = blit_texture_;
            const tjs_int pitch = tex->GetPitch();
            const void* pixelData = tex->GetPixelData();
            if (!pixelData) {
                // Fallback: read line by line via GetScanLineForRead.
                // NOTE: This may call TVPSetRenderTarget() internally,
                // which changes the current FBO binding — that's fine
                // because we haven't bound the IOSurface FBO yet.
                if (blit_pixel_buf_.size() < static_cast<size_t>(tw * th * 4)) {
                    blit_pixel_buf_.resize(tw * th * 4);
                }
                for (tjs_uint y = 0; y < th; ++y) {
                    const void* line = tex->GetScanLineForRead(y);
                    if (line) {
                        std::memcpy(blit_pixel_buf_.data() + y * tw * 4, line, tw * 4);
                    }
                }
                pixelData = blit_pixel_buf_.data();
            }

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, blit_texture_);
            // Use GL_UNPACK_ROW_LENGTH if pitch differs from width*4
            if (pitch != static_cast<tjs_int>(tw * 4)) {
                glPixelStorei(GL_UNPACK_ROW_LENGTH, pitch / 4);
            }
            // Use glTexSubImage2D when the texture size hasn't changed,
            // avoiding per-frame texture memory reallocation.
            if (blit_tex_w_ == tw && blit_tex_h_ == th) {
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                                static_cast<GLsizei>(tw), static_cast<GLsizei>(th),
                                GL_RGBA, GL_UNSIGNED_BYTE, pixelData);
            } else {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                             static_cast<GLsizei>(tw), static_cast<GLsizei>(th),
                             0, GL_RGBA, GL_UNSIGNED_BYTE, pixelData);
                blit_tex_w_ = tw;
                blit_tex_h_ = th;
            }
            if (pitch != static_cast<tjs_int>(tw * 4)) {
                glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            }
        }

        // ── Phase 2: Bind IOSurface render target and blit ───────
        // Now that the source texture is ready, switch to the
        // IOSurface FBO (or Pbuffer) for the actual blit output.
        egl.BindRenderTarget();

        // Determine the actual render target dimensions
        uint32_t fbW, fbH;
        if (egl.HasIOSurface()) {
            fbW = egl.GetIOSurfaceWidth();
            fbH = egl.GetIOSurfaceHeight();
        } else if (egl.HasNativeWindow()) {
            fbW = egl.GetNativeWindowWidth();
            fbH = egl.GetNativeWindowHeight();
        } else {
            fbW = egl.GetWidth();
            fbH = egl.GetHeight();
        }
        // Compute letterbox/pillarbox viewport to preserve game aspect ratio.
        float texAspect = static_cast<float>(tw) / static_cast<float>(th);
        float fbAspect  = static_cast<float>(fbW) / static_cast<float>(fbH);
        GLsizei vpX = 0, vpY = 0;
        GLsizei vpW = static_cast<GLsizei>(fbW);
        GLsizei vpH = static_cast<GLsizei>(fbH);
        if (texAspect > fbAspect) {
            vpW = static_cast<GLsizei>(fbW);
            vpH = static_cast<GLsizei>(static_cast<float>(fbW) / texAspect);
            vpY = static_cast<GLsizei>((fbH - vpH) / 2);
        } else if (texAspect < fbAspect) {
            vpH = static_cast<GLsizei>(fbH);
            vpW = static_cast<GLsizei>(static_cast<float>(fbH) * texAspect);
            vpX = static_cast<GLsizei>((fbW - vpW) / 2);
        }

        // Clear entire framebuffer to black (produces the letterbox bars)
        glViewport(0, 0, static_cast<GLsizei>(fbW), static_cast<GLsizei>(fbH));
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Set viewport to the aspect-correct sub-region
        glViewport(vpX, vpY, vpW, vpH);

        // Update DrawDevice dest rect so coordinate transforms
        // (surface pixels → game layer) work correctly with letterbox offset.
        if (owner_) {
            auto* dd = owner_->GetDrawDevice();
            if (dd) {
                tTVPRect dest;
                dest.left   = static_cast<tjs_int>(vpX);
                dest.top    = static_cast<tjs_int>(vpY);
                dest.right  = static_cast<tjs_int>(vpX + vpW);
                dest.bottom = static_cast<tjs_int>(vpY + vpH);
                dd->SetDestRectangle(dest);
                dd->SetClipRectangle(dest);
                dd->SetViewport(dest);
                dd->SetWindowSize(static_cast<tjs_int>(fbW),
                                  static_cast<tjs_int>(fbH));
            }
        }

        // Bind the source texture for the fullscreen blit
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, blitSrcTexture);

        // Draw fullscreen quad
        glUseProgram(blit_program_);
        glUniform1i(blit_tex_uniform_, 0);
        // In IOSurface mode, the surface has a top-down coordinate system
        // while OpenGL renders bottom-up, so we need to flip Y.
        // Android SurfaceTexture (WindowSurface) does NOT need flipping
        // because eglSwapBuffers → SurfaceTexture → Flutter Texture widget
        // handles the coordinate transform automatically.
        // When using a native OGL texture from the engine (GPU path), the
        // texture is already in OGL convention (bottom-up), so we may need
        // to flip when rendering to IOSurface but not to Pbuffer/WindowSurface.
        glUniform1f(blit_flipy_uniform_, egl.HasIOSurface() ? 1.0f : 0.0f);

        // Compute UV scale to handle power-of-two textures.
        // The engine texture's logical size (tw x th) may be smaller
        // than the actual GL texture (internalW x internalH).
        float uvScaleU = 1.0f, uvScaleV = 1.0f;
        if (nativeGLTex != 0) {
            const tjs_uint intW = tex->GetInternalWidth();
            const tjs_uint intH = tex->GetInternalHeight();
            if (intW > 0 && intH > 0) {
                uvScaleU = static_cast<float>(tw) / static_cast<float>(intW);
                uvScaleV = static_cast<float>(th) / static_cast<float>(intH);
            }
        }
        glUniform2f(blit_uvscale_uniform_, uvScaleU, uvScaleV);

        glBindBuffer(GL_ARRAY_BUFFER, blit_vbo_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                              (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                              (void*)(2 * sizeof(float)));

        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_SCISSOR_TEST);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glUseProgram(0);
        glBindTexture(GL_TEXTURE_2D, 0);

        if (g_postDrawHook) g_postDrawHook();

        // In IOSurface/WindowSurface mode, glFlush() is sufficient —
        // IOSurface has GPU-GPU sync, and WindowSurface (SurfaceTexture)
        // is synchronized by eglSwapBuffers in TVPForceSwapBuffer.
        // In Pbuffer mode, glFinish() is required because the legacy
        // path uses glReadPixels which needs GPU to be done.
        if (egl.HasIOSurface() || egl.HasNativeWindow()) {
            glFlush();
        } else {
            glFinish();
        }

        // Mark the frame as dirty so TVPForceSwapBuffer() knows there is
        // new content to present.  Without this, eglSwapBuffers would be
        // called every tick even when no rendering happened, causing
        // double-buffer flicker (alternating between current and stale
        // back-buffer contents).
        egl.MarkFrameDirty();
    }

    void InvalidateClose() override {
        closing_ = true;
    }

    bool GetWindowActive() override { return active_; }

    void Close() override {
        closing_ = true;
        spdlog::debug("FlutterWindowLayer::Close called");
        TVPTerminateAsync(0);
    }

    void OnCloseQueryCalled(bool b) override {
        if (b) {
            Close();
        }
    }

    void InternalKeyDown(tjs_uint16 key, tjs_uint32 shift) override {}

    void OnKeyUp(tjs_uint16 vk, int shift) override {}

    void OnKeyPress(tjs_uint16 vk, int repeat, bool prevkeystate,
                    bool convertkey) override {}

    [[nodiscard]] tTVPImeMode GetDefaultImeMode() const override {
        return imDisable;
    }

    void SetImeMode(tTVPImeMode mode) override {}

    void ResetImeMode() override {}

    void UpdateWindow(tTVPUpdateType type) override {
        // Rendering is driven by engine_tick / engine_read_frame_rgba
    }

    void SetVisibleFromScript(bool b) override { visible_ = b; }

    void SetUseMouseKey(bool b) override {}

    [[nodiscard]] bool GetUseMouseKey() const override { return false; }

    void ResetMouseVelocity() override {}

    void ResetTouchVelocity(tjs_int id) override {}

    bool GetMouseVelocity(float &x, float &y, float &speed) const override {
        x = y = speed = 0;
        return false;
    }

    void TickBeat() override {
        // Called every ~50ms; nothing to do in Flutter mode
    }

    TVPOverlayNode *GetPrimaryArea() override { return nullptr; }

private:
    void EnsureBlitResources() {
        if (blit_program_ != 0) return;

        // Vertex shader: fullscreen quad with optional Y flip and UV scale
        const char* vs_src = R"(#version 300 es
            layout(location = 0) in vec2 aPos;
            layout(location = 1) in vec2 aUV;
            uniform float uFlipY;
            uniform vec2 uUVScale;
            out vec2 vUV;
            void main() {
                gl_Position = vec4(aPos, 0.0, 1.0);
                vec2 uv = aUV * uUVScale;
                vUV = vec2(uv.x, mix(uv.y, uUVScale.y - uv.y, uFlipY));
            }
        )";

        const char* fs_src = R"(#version 300 es
            precision mediump float;
            in vec2 vUV;
            out vec4 fragColor;
            uniform sampler2D uTex;
            void main() {
                fragColor = texture(uTex, vUV);
            }
        )";

        auto compileShader = [](GLenum type, const char* src) -> GLuint {
            GLuint s = glCreateShader(type);
            glShaderSource(s, 1, &src, nullptr);
            glCompileShader(s);
            GLint ok = 0;
            glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                char log[512];
                glGetShaderInfoLog(s, sizeof(log), nullptr, log);
                spdlog::error("Blit shader compile error: {}", log);
            }
            return s;
        };

        GLuint vs = compileShader(GL_VERTEX_SHADER, vs_src);
        GLuint fs = compileShader(GL_FRAGMENT_SHADER, fs_src);

        blit_program_ = glCreateProgram();
        glAttachShader(blit_program_, vs);
        glAttachShader(blit_program_, fs);
        glLinkProgram(blit_program_);

        GLint ok = 0;
        glGetProgramiv(blit_program_, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[512];
            glGetProgramInfoLog(blit_program_, sizeof(log), nullptr, log);
            spdlog::error("Blit program link error: {}", log);
        }

        glDeleteShader(vs);
        glDeleteShader(fs);

        blit_tex_uniform_ = glGetUniformLocation(blit_program_, "uTex");
        blit_flipy_uniform_ = glGetUniformLocation(blit_program_, "uFlipY");
        blit_uvscale_uniform_ = glGetUniformLocation(blit_program_, "uUVScale");

        // Fullscreen quad: position (x,y) + texcoord (u,v)
        // Y-flipped: top-left of texture → top-left of screen
        // OpenGL NDC: bottom-left is (-1,-1), top-right is (1,1)
        // Texture: (0,0) is top-left in krkr2 convention
        const float quad[] = {
            // pos        // uv
            -1.f, -1.f,   0.f, 1.f,  // bottom-left  → tex bottom (v=1)
             1.f, -1.f,   1.f, 1.f,  // bottom-right
            -1.f,  1.f,   0.f, 0.f,  // top-left     → tex top (v=0)
             1.f,  1.f,   1.f, 0.f,  // top-right
        };

        glGenBuffers(1, &blit_vbo_);
        glBindBuffer(GL_ARRAY_BUFFER, blit_vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        // Create blit texture
        glGenTextures(1, &blit_texture_);
        glBindTexture(GL_TEXTURE_2D, blit_texture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);

        spdlog::info("FlutterWindowLayer: blit resources initialized (program={})",
                     blit_program_);
    }

    tTJSNI_Window *owner_;
    bool visible_;
    std::string caption_;
    tjs_int width_;
    tjs_int height_;
    bool active_;
    bool closing_;

    // Cached mouse position in surface coordinates.
    // Updated by EngineLoop on pointer events, read by GetCursorPos().
    tjs_int last_mouse_x_ = 0;
    tjs_int last_mouse_y_ = 0;

    // Blit resources for rendering to EGL pbuffer
    GLuint blit_program_ = 0;
    GLuint blit_vbo_ = 0;
    GLuint blit_texture_ = 0;
    tjs_uint blit_tex_w_ = 0;   // Last allocated texture width
    tjs_uint blit_tex_h_ = 0;   // Last allocated texture height
    GLint  blit_tex_uniform_ = -1;
    GLint  blit_flipy_uniform_ = -1;
    GLint  blit_uvscale_uniform_ = -1;
    std::vector<uint8_t> blit_pixel_buf_;
};

// ---------------------------------------------------------------------------
// TVPInitUIExtension — originally in ui/extension/UIExtension.cpp
// Registered custom UI widgets (PageView, etc.)
// ---------------------------------------------------------------------------
void TVPInitUIExtension() {
    spdlog::debug("TVPInitUIExtension: stub (UI handled by Flutter)");
}

// ---------------------------------------------------------------------------
// TVPCreateAndAddWindow — originally in MainScene.cpp
// Creates a FlutterWindowLayer and registers it with the application.
// ---------------------------------------------------------------------------
iWindowLayer *TVPCreateAndAddWindow(tTJSNI_Window *w) {
    auto *layer = new FlutterWindowLayer(w);
    spdlog::info("TVPCreateAndAddWindow: created FlutterWindowLayer ({}x{})",
                 layer->GetWidth(), layer->GetHeight());
    return layer;
}

// ---------------------------------------------------------------------------
// TVPConsoleLog — originally in MainScene.cpp
// Logs engine console output. Redirect to spdlog.
// ---------------------------------------------------------------------------
void TVPConsoleLog(const ttstr &mes, bool important) {
    // Convert TJS string to UTF-8 for spdlog
    tTJSNarrowStringHolder narrow_mes(mes.c_str());
    if (important) {
        spdlog::info("[TVP Console] {}", narrow_mes.operator const char *());
    } else {
        spdlog::debug("[TVP Console] {}", narrow_mes.operator const char *());
    }
}

// ---------------------------------------------------------------------------
// TJS::TVPConsoleLog — originally in MainScene.cpp (TJS2 namespace version)
// ---------------------------------------------------------------------------
namespace TJS {
void TVPConsoleLog(const tTJSString &str) {
    tTJSNarrowStringHolder narrow(str.c_str());
    spdlog::debug("[TJS Console] {}", narrow.operator const char *());
}
} // namespace TJS

// ---------------------------------------------------------------------------
// TVPGetOSName / TVPGetPlatformName — originally in MainScene.cpp
// Returns OS/platform identification strings.
// ---------------------------------------------------------------------------
ttstr TVPGetOSName() {
#if defined(__APPLE__)
    return ttstr(TJS_W("macOS"));
#elif defined(_WIN32)
    return ttstr(TJS_W("Windows"));
#elif defined(__linux__)
    return ttstr(TJS_W("Linux"));
#else
    return ttstr(TJS_W("Unknown"));
#endif
}

ttstr TVPGetPlatformName() {
#if defined(__aarch64__) || defined(_M_ARM64)
    return ttstr(TJS_W("ARM64"));
#elif defined(__x86_64__) || defined(_M_X64)
    return ttstr(TJS_W("x86_64"));
#else
    return ttstr(TJS_W("Unknown"));
#endif
}

// ---------------------------------------------------------------------------
// TVPGetInternalPreferencePath — originally in MainScene.cpp
// Returns the directory path for storing preferences/config files.
// ---------------------------------------------------------------------------
static std::string s_internalPreferencePath;

const std::string &TVPGetInternalPreferencePath() {
    if (s_internalPreferencePath.empty()) {
#if defined(__APPLE__)
        const char *home = getenv("HOME");
        if (home) {
            s_internalPreferencePath = std::string(home) + "/Library/Application Support/krkr2/";
        } else {
            s_internalPreferencePath = "/tmp/krkr2/";
        }
#elif defined(__ANDROID__)
        // On Android, /tmp does not exist. Use the app's private data directory.
        // Read package name from /proc/self/cmdline to build the path.
        std::string packageName;
        {
            std::ifstream cmdline("/proc/self/cmdline");
            if (cmdline.is_open()) {
                std::getline(cmdline, packageName, '\0');
            }
        }
        if (!packageName.empty()) {
            s_internalPreferencePath = "/data/data/" + packageName + "/files/krkr2/";
        } else {
            // Fallback: use a path that Android apps can typically write to
            s_internalPreferencePath = "/data/local/tmp/krkr2/";
        }
#else
        s_internalPreferencePath = "/tmp/krkr2/";
#endif
        std::filesystem::create_directories(s_internalPreferencePath);
    }
    return s_internalPreferencePath;
}

// ---------------------------------------------------------------------------
// TVPGetApplicationHomeDirectory — originally in MainScene.cpp
// Returns list of directories where the application searches for data files.
// ---------------------------------------------------------------------------
static std::vector<std::string> s_appHomeDirs;

const std::vector<std::string> &TVPGetApplicationHomeDirectory() {
    if (s_appHomeDirs.empty()) {
        if (!TVPNativeProjectDir.IsEmpty()) {
            std::string dir = TVPNativeProjectDir.AsNarrowStdString();
            while (!dir.empty() && dir.back() == '/')
                dir.pop_back();
            s_appHomeDirs.push_back(dir);
        } else {
            s_appHomeDirs.push_back(std::filesystem::current_path().string());
        }
    }
    return s_appHomeDirs;
}

// ---------------------------------------------------------------------------
// TVPCopyFile — originally in CustomFileUtils.cpp
// Copies a file from source to destination.
// ---------------------------------------------------------------------------
bool TVPCopyFile(const std::string &from, const std::string &to) {
    std::error_code ec;
    std::filesystem::copy_file(from, to,
        std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        spdlog::error("TVPCopyFile failed: {} -> {} ({})", from, to, ec.message());
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// TVPShowFileSelector — originally in ui/FileSelectorForm.cpp
// Shows a file selection dialog. In Flutter mode, this is handled by the
// Flutter host layer. Returns empty string (no selection).
// ---------------------------------------------------------------------------
std::string TVPShowFileSelector(const std::string &title,
                                const std::string &init_dir,
                                std::string default_ext,
                                bool is_save) {
    spdlog::warn("TVPShowFileSelector: stub — file selection handled by Flutter");
    return "";
}

// ---------------------------------------------------------------------------
// TVPShowPopMenu — originally in ui/InGameMenuForm.cpp
// Shows a popup context menu. In Flutter mode, handled by Flutter host.
// ---------------------------------------------------------------------------
void TVPShowPopMenu(tTJSNI_MenuItem *menu) {
    spdlog::warn("TVPShowPopMenu: stub — popup menus handled by Flutter");
}

// ---------------------------------------------------------------------------
// TVPOpenPatchLibUrl — originally in AppDelegate.cpp
// Opens the URL for the patch library website.
// ---------------------------------------------------------------------------
void TVPOpenPatchLibUrl() {
    spdlog::warn("TVPOpenPatchLibUrl: stub — URL opening handled by Flutter");
}
