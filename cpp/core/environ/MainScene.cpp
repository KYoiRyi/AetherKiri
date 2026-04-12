/**
 * @file MainScene.cpp
 * @brief Compatibility wrapper — delegates to EngineLoop.
 *
 * TVPMainScene is kept as a thin compatibility layer so that existing code
 * (engine_api.cpp, standalone main.cpp) can still reference TVPMainScene.
 * All real logic now lives in EngineLoop (Phase 3).
 */

#include "MainScene.h"
#include "EngineLoop.h"

#include <spdlog/spdlog.h>

// ---------------------------------------------------------------------------
// Global functions — now defined in EngineLoop.cpp, only declared here
// for backward compatibility with headers that expect them in MainScene.
// (TVPSetPostUpdateEvent, TVPGetKeyMouseAsyncState, TVPGetJoyPadAsyncState,
//  TVPDrawSceneOnce are all implemented in EngineLoop.cpp)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// TVPMainScene implementation — thin delegation to EngineLoop
// ---------------------------------------------------------------------------

static TVPMainScene* _instance = nullptr;

TVPMainScene::TVPMainScene() = default;
TVPMainScene::~TVPMainScene() {
    if (_instance == this) {
        _instance = nullptr;
    }
}

TVPMainScene* TVPMainScene::GetInstance() { return _instance; }

TVPMainScene* TVPMainScene::CreateInstance() {
    if (!_instance) {
        _instance = new TVPMainScene();
        // Ensure EngineLoop singleton is also created
        EngineLoop::CreateInstance();
    }
    return _instance;
}

void TVPMainScene::DestroyInstance() {
    if (!_instance) {
        return;
    }
    delete _instance;
    _instance = nullptr;
}

void TVPMainScene::scheduleUpdate() {
    _updateScheduled = true;
    if (auto* loop = EngineLoop::GetInstance()) {
        loop->Start();
    }
}

void TVPMainScene::update(float delta) {
    if (auto* loop = EngineLoop::GetInstance()) {
        loop->Tick(delta);
    }
}

bool TVPMainScene::startupFrom(const std::string& path) {
    if (auto* loop = EngineLoop::GetInstance()) {
        bool result = loop->StartupFrom(path);
        if (result) {
            _started = true;
            _updateScheduled = true;
        }
        return result;
    }
    return false;
}

void TVPMainScene::doStartup(const std::string& path) {
    // Delegated — EngineLoop::StartupFrom calls DoStartup internally
}
