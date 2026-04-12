/**
 * @file EngineLoop.h
 * @brief Core engine loop driver — replaces the original Scene-based
 *        MainScene with a clean, platform-agnostic loop + input dispatcher.
 *
 * Responsibilities:
 *   - Drive Application::Run() each frame (called by engine_tick)
 *   - Forward input events from engine_api → engine core (TVPPostInputEvent)
 *   - Maintain async key state (_scancode[]) for TJS2 System.getKeyState
 *
 * This class is the Phase 3 replacement for TVPMainScene.
 */
#pragma once

#include <cstdint>
#include <string>

// ---------------------------------------------------------------------------
// Input event types (mirror of engine_api.h, kept separate to avoid
// a dependency from core → bridge).
// ---------------------------------------------------------------------------
enum EngineInputEventType : uint32_t {
    kEngineInputPointerDown  = 1,
    kEngineInputPointerMove  = 2,
    kEngineInputPointerUp    = 3,
    kEngineInputPointerScroll = 4,
    kEngineInputKeyDown      = 5,
    kEngineInputKeyUp        = 6,
    kEngineInputTextInput    = 7,
    kEngineInputBack         = 8,
};

/**
 * Lightweight input event structure used to pass events from engine_api
 * into the core engine loop. This avoids a dependency on engine_api.h.
 */
struct EngineInputEvent {
    uint32_t type = 0;          ///< EngineInputEventType
    double   x = 0;             ///< Pointer X in logical pixels
    double   y = 0;             ///< Pointer Y in logical pixels
    double   delta_x = 0;       ///< Scroll delta X
    double   delta_y = 0;       ///< Scroll delta Y
    int32_t  pointer_id = 0;    ///< Pointer / touch ID
    int32_t  button = 0;        ///< Mouse button: 0=left, 1=right, 2=middle
    int32_t  key_code = 0;      ///< Virtual key code (Windows VK_*)
    int32_t  modifiers = 0;     ///< Shift state flags (TVP_SS_* compatible)
    uint32_t unicode_codepoint = 0; ///< Unicode codepoint for text input
};

/**
 * @class EngineLoop
 * @brief Singleton engine loop driver with input event forwarding.
 *
 * Lifecycle:
 *   1. EngineLoop::CreateInstance()        — from engine_open_game
 *   2. EngineLoop::StartupFrom(path)       — optional standalone path
 *   3. EngineLoop::Start()                 — enable frame updates
 *   4. EngineLoop::Tick(delta)             — called every frame
 *   5. EngineLoop::HandleInputEvent(event) — called before each tick
 */
class EngineLoop {
    EngineLoop();

public:
    ~EngineLoop();

    // Non-copyable
    EngineLoop(const EngineLoop&) = delete;
    EngineLoop& operator=(const EngineLoop&) = delete;

    /** Get the singleton instance (nullptr if not created). */
    static EngineLoop* GetInstance();

    /** Create the singleton instance (idempotent). */
    static EngineLoop* CreateInstance();
    static void DestroyInstance();

    /**
     * Start the engine from the given game path (standalone mode).
     * In host mode (Flutter), engine_open_game calls Application::StartApplication
     * directly, so this may not be used.
     */
    bool StartupFrom(const std::string& path);

    /**
     * Enable per-frame updates.
     * Called by engine_open_game after the game is loaded.
     */
    void Start();

    /**
     * Main loop tick — drives Application::Run() + texture recycling.
     * Called by engine_tick() or the host frame callback.
     * @param delta  Time elapsed since last tick, in seconds.
     */
    void Tick(float delta);

    /** Whether the loop has been started. */
    bool IsStarted() const { return started_; }

    // -----------------------------------------------------------------------
    // Input event handling
    // -----------------------------------------------------------------------

    /**
     * Dispatch a single input event to the engine core.
     *
     * Converts EngineInputEvent → TVP input events and posts them
     * via TVPPostInputEvent. Also maintains scancode[] for async key state.
     *
     * @param event  The input event from engine_api.
     * @return true on success, false if the event could not be dispatched
     *         (e.g. no active window).
     */
    bool HandleInputEvent(const EngineInputEvent& event);

private:
    void DoStartup(const std::string& path);

    // Input helpers
    void HandlePointerDown(const EngineInputEvent& event);
    void HandlePointerMove(const EngineInputEvent& event);
    void HandlePointerUp(const EngineInputEvent& event);
    void HandlePointerScroll(const EngineInputEvent& event);
    void HandleKeyDown(const EngineInputEvent& event);
    void HandleKeyUp(const EngineInputEvent& event);
    void HandleTextInput(const EngineInputEvent& event);

    /**
     * Convert modifier flags to TVP shift state flags (TVP_SS_*).
     */
    static uint32_t ConvertModifiers(int32_t modifiers);

    bool started_ = false;
    bool update_enabled_ = false;

    // Stored mouse-down position for click event (mirrors original engine's
    // LastMouseDownX/Y).  OnClick uses the down position, not up position.
    int32_t last_mouse_down_x_ = 0;
    int32_t last_mouse_down_y_ = 0;
};
