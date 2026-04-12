/**
 * @file EngineLoop.cpp
 * @brief EngineLoop implementation — engine main loop + input event forwarding.
 *
 * This is the Phase 3 core: it drives Application::Run() per frame and
 * converts EngineInputEvent into TVP input events posted to the
 * engine's event queue.
 */

#include "EngineLoop.h"

#include <spdlog/spdlog.h>

#include "Application.h"
#include "ConfigManager/IndividualConfigManager.h"
#include "ConfigManager/GlobalConfigManager.h"
#include "Platform.h"
#include "SysInitIntf.h"
#include "RenderManager.h"
#include "TickCount.h"
#ifdef __APPLE__
#include <malloc/malloc.h>
#endif

// TVP input event classes + TVPPostInputEvent
#include "WindowIntf.h"
#include "WindowImpl.h"
#include "tvpinputdefs.h"
#include "EventIntf.h"

// Forward declarations for functions used by the engine core
extern bool TVPCheckStartupPath(const std::string& path);
extern void TVPForceSwapBuffer();

// ---------------------------------------------------------------------------
// Global state — previously in MainScene.cpp, now owned by EngineLoop
// ---------------------------------------------------------------------------

static void (*s_postUpdate)() = nullptr;
void TVPSetPostUpdateEvent(void (*f)()) { s_postUpdate = f; }

// Async key/mouse state table — indexed by Windows VK code
// Bit 0 = currently pressed, Bit 4 = was pressed since last query
static tjs_uint8 s_scancode[0x200] = {};

bool TVPGetKeyMouseAsyncState(tjs_uint keycode, bool getcurrent) {
    if (keycode >= sizeof(s_scancode) / sizeof(s_scancode[0]))
        return false;
    tjs_uint8 code = s_scancode[keycode];
    s_scancode[keycode] &= 1;
    return code & (getcurrent ? 1 : 0x10);
}

bool TVPGetJoyPadAsyncState(tjs_uint keycode, bool getcurrent) {
    if (keycode >= sizeof(s_scancode) / sizeof(s_scancode[0]))
        return false;
    tjs_uint8 code = s_scancode[keycode];
    s_scancode[keycode] &= 1;
    return code & (getcurrent ? 1 : 0x10);
}

int TVPDrawSceneOnce(int interval) {
    static tjs_uint64 lastTick = TVPGetRoughTickCount32();
    tjs_uint64 curTick = TVPGetRoughTickCount32();
    int remain = interval - static_cast<int>(curTick - lastTick);
    if (remain <= 0) {
        if (s_postUpdate)
            s_postUpdate();
        TVPForceSwapBuffer();
        lastTick = curTick;
        return 0;
    } else {
        return remain;
    }
}

// ---------------------------------------------------------------------------
// EngineLoop singleton
// ---------------------------------------------------------------------------

static EngineLoop* s_instance = nullptr;

EngineLoop::EngineLoop() = default;

EngineLoop::~EngineLoop() {
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

EngineLoop* EngineLoop::GetInstance() {
    return s_instance;
}

EngineLoop* EngineLoop::CreateInstance() {
    if (!s_instance) {
        s_instance = new EngineLoop();
    }
    return s_instance;
}

void EngineLoop::DestroyInstance() {
    if (s_instance) {
        delete s_instance;
        s_instance = nullptr;
    }

    std::fill_n(s_scancode, sizeof(s_scancode) / sizeof(s_scancode[0]), 0);
    s_postUpdate = nullptr;
}

void EngineLoop::Start() {
    update_enabled_ = true;
}

void EngineLoop::Tick(float delta) {
    if (!started_)
        return;
    ::Application->Run();
    iTVPTexture2D::RecycleProcess();
    if (s_postUpdate)
        s_postUpdate();
}

bool EngineLoop::StartupFrom(const std::string& path) {
    if (!TVPCheckStartupPath(path)) {
        return false;
    }

    IndividualConfigManager* pCfgMgr = IndividualConfigManager::GetInstance();
    auto sepPos = path.find_last_of("/\\");
    if (sepPos != std::string::npos) {
        pCfgMgr->UsePreferenceAt(path.substr(0, sepPos));
    }

    DoStartup(path);
    return true;
}

void EngineLoop::DoStartup(const std::string& path) {
    spdlog::info("EngineLoop::DoStartup starting game from: {}", path);

    ::Application->StartApplication(path);

#ifdef __APPLE__
    malloc_zone_pressure_relief(nullptr, 0);
#endif

    // Run one frame immediately (matches original behavior)
    Tick(0);

    started_ = true;
    update_enabled_ = true;

    spdlog::info("EngineLoop::DoStartup complete");
}

// ---------------------------------------------------------------------------
// Input event handling
// ---------------------------------------------------------------------------

uint32_t EngineLoop::ConvertModifiers(int32_t modifiers) {
    // engine_api modifiers use the same bit layout as TVP_SS_* flags:
    //   bit 0 = Shift  (TVP_SS_SHIFT = 0x01)
    //   bit 1 = Alt    (TVP_SS_ALT   = 0x02)
    //   bit 2 = Ctrl   (TVP_SS_CTRL  = 0x04)
    //   bit 3 = Left   (TVP_SS_LEFT  = 0x08)
    //   bit 4 = Right  (TVP_SS_RIGHT = 0x10)
    //   bit 5 = Middle (TVP_SS_MIDDLE= 0x20)
    return static_cast<uint32_t>(modifiers) & 0xFF;
}

bool EngineLoop::HandleInputEvent(const EngineInputEvent& event) {
    switch (event.type) {
        case kEngineInputPointerDown:
            HandlePointerDown(event);
            return true;
        case kEngineInputPointerMove:
            HandlePointerMove(event);
            return true;
        case kEngineInputPointerUp:
            HandlePointerUp(event);
            return true;
        case kEngineInputPointerScroll:
            HandlePointerScroll(event);
            return true;
        case kEngineInputKeyDown:
            HandleKeyDown(event);
            return true;
        case kEngineInputKeyUp:
            HandleKeyUp(event);
            return true;
        case kEngineInputTextInput:
            HandleTextInput(event);
            return true;
        case kEngineInputBack:
            // Treat "Back" as Escape key press
            HandleKeyDown(event);
            return true;
        default:
            spdlog::warn("EngineLoop::HandleInputEvent: unknown event type {}",
                         event.type);
            return false;
    }
}

void EngineLoop::HandlePointerDown(const EngineInputEvent& event) {
    auto* win = TVPMainWindow;
    if (!win) return;

    const tjs_int x = static_cast<tjs_int>(event.x);
    const tjs_int y = static_cast<tjs_int>(event.y);
    const uint32_t shift = ConvertModifiers(event.modifiers);

    // Update cached cursor position for Layer.cursorX/cursorY queries
    if (win->GetForm())
        win->GetForm()->UpdateCursorPos(x, y);

    // Map button index: 0=left, 1=right, 2=middle (matches tTVPMouseButton)
    tTVPMouseButton mb = mbLeft;
    if (event.button == 1)
        mb = mbRight;
    else if (event.button == 2)
        mb = mbMiddle;

    // Update scancode for mouse button async state
    uint16_t vk = 0;
    switch (mb) {
        case mbLeft:   vk = 0x01; break; // VK_LBUTTON
        case mbRight:  vk = 0x02; break; // VK_RBUTTON
        case mbMiddle: vk = 0x04; break; // VK_MBUTTON
        default: break;
    }
    if (vk < sizeof(s_scancode) / sizeof(s_scancode[0])) {
        s_scancode[vk] = 0x11; // pressed + was-pressed
    }

    // Combine mouse button state into shift flags
    uint32_t flags = shift;
    switch (mb) {
        case mbLeft:   flags |= TVP_SS_LEFT;   break;
        case mbRight:  flags |= TVP_SS_RIGHT;  break;
        case mbMiddle: flags |= TVP_SS_MIDDLE; break;
        default: break;
    }

    last_mouse_down_x_ = x;
    last_mouse_down_y_ = y;

    TVPPostInputEvent(
        new tTVPOnMouseDownInputEvent(win, x, y, mb, flags));
}

void EngineLoop::HandlePointerMove(const EngineInputEvent& event) {
    auto* win = TVPMainWindow;
    if (!win) return;

    const tjs_int x = static_cast<tjs_int>(event.x);
    const tjs_int y = static_cast<tjs_int>(event.y);
    const uint32_t shift = ConvertModifiers(event.modifiers);

    // Update cached cursor position for Layer.cursorX/cursorY queries
    if (win->GetForm())
        win->GetForm()->UpdateCursorPos(x, y);

    TVPPostInputEvent(
        new tTVPOnMouseMoveInputEvent(win, x, y, shift));
}

void EngineLoop::HandlePointerUp(const EngineInputEvent& event) {
    auto* win = TVPMainWindow;
    if (!win) return;

    const tjs_int x = static_cast<tjs_int>(event.x);
    const tjs_int y = static_cast<tjs_int>(event.y);
    const uint32_t shift = ConvertModifiers(event.modifiers);

    // Update cached cursor position for Layer.cursorX/cursorY queries
    if (win->GetForm())
        win->GetForm()->UpdateCursorPos(x, y);

    tTVPMouseButton mb = mbLeft;
    if (event.button == 1)
        mb = mbRight;
    else if (event.button == 2)
        mb = mbMiddle;

    // Update scancode: clear pressed bit
    uint16_t vk = 0;
    switch (mb) {
        case mbLeft:   vk = 0x01; break;
        case mbRight:  vk = 0x02; break;
        case mbMiddle: vk = 0x04; break;
        default: break;
    }
    if (vk < sizeof(s_scancode) / sizeof(s_scancode[0])) {
        s_scancode[vk] &= ~1; // clear current-pressed, keep was-pressed
    }

    TVPPostInputEvent(
        new tTVPOnMouseUpInputEvent(win, x, y, mb, shift));

    // Post click event after mouse-up, matching the original Windows engine
    // which fires OnMouseClick (→ Window.onClick → PrimaryClick) after
    // OnMouseUp.  Uses the stored down position per original behavior.
    if (mb == mbLeft) {
        TVPPostInputEvent(
            new tTVPOnClickInputEvent(win, last_mouse_down_x_,
                                      last_mouse_down_y_));
    }
}

void EngineLoop::HandlePointerScroll(const EngineInputEvent& event) {
    auto* win = TVPMainWindow;
    if (!win) return;

    const tjs_int x = static_cast<tjs_int>(event.x);
    const tjs_int y = static_cast<tjs_int>(event.y);
    const uint32_t shift = ConvertModifiers(event.modifiers);

    // delta_y > 0 = scroll up, delta_y < 0 = scroll down
    // TVP expects wheel delta in units (positive = up)
    const tjs_int delta = static_cast<tjs_int>(event.delta_y * 120.0);

    if (delta != 0) {
        TVPPostInputEvent(
            new tTVPOnMouseWheelInputEvent(win, shift, delta, x, y));
    }
}

void EngineLoop::HandleKeyDown(const EngineInputEvent& event) {
    auto* win = TVPMainWindow;
    if (!win) return;

    tjs_uint key = static_cast<tjs_uint>(event.key_code);

    // For BACK button, map to VK_ESCAPE
    if (event.type == kEngineInputBack) {
        key = 0x1B; // VK_ESCAPE
    }

    const uint32_t shift = ConvertModifiers(event.modifiers);

    // Update scancode state
    if (key < sizeof(s_scancode) / sizeof(s_scancode[0])) {
        s_scancode[key] = 0x11; // pressed + was-pressed
    }

    TVPPostInputEvent(
        new tTVPOnKeyDownInputEvent(win, key, shift));
}

void EngineLoop::HandleKeyUp(const EngineInputEvent& event) {
    auto* win = TVPMainWindow;
    if (!win) return;

    const tjs_uint key = static_cast<tjs_uint>(event.key_code);
    const uint32_t shift = ConvertModifiers(event.modifiers);

    // Update scancode: clear pressed bit
    if (key < sizeof(s_scancode) / sizeof(s_scancode[0])) {
        s_scancode[key] &= ~1;
    }

    TVPPostInputEvent(
        new tTVPOnKeyUpInputEvent(win, key, shift));
}

void EngineLoop::HandleTextInput(const EngineInputEvent& event) {
    auto* win = TVPMainWindow;
    if (!win) return;

    if (event.unicode_codepoint > 0 && event.unicode_codepoint <= 0xFFFF) {
        const tjs_char ch = static_cast<tjs_char>(event.unicode_codepoint);
        TVPPostInputEvent(
            new tTVPOnKeyPressInputEvent(win, ch));
    }
}
