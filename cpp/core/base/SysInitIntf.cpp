//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// System Initialization and Uninitialization
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include <vector>
#include <algorithm>
#include <functional>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#include "tjsUtils.h"
#include "SysInitIntf.h"
#include "ScriptMgnIntf.h"
#include "tvpgl.h"

//---------------------------------------------------------------------------
// global data
//---------------------------------------------------------------------------
ttstr TVPProjectDir; // project directory (in unified storage name)
ttstr TVPDataPath; // data directory (in unified storage name)
//---------------------------------------------------------------------------

extern void TVPGL_C_Init();

namespace {

constexpr const char* kExitTracePath = "/tmp/aetherkiri-exit-trace.log";

void AppendExitTrace(const char* message) {
    if(message == nullptr)
        return;
    const int fd = ::open(kExitTracePath, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if(fd < 0)
        return;

    char buffer[512] = {0};
    const int written =
        std::snprintf(buffer, sizeof(buffer), "pid=%d %s\n",
                      static_cast<int>(::getpid()), message);
    if(written > 0) {
        (void)::write(fd, buffer, static_cast<size_t>(written));
        (void)::fsync(fd);
    }
    (void)::close(fd);
}

} // namespace

//---------------------------------------------------------------------------
// TVPSystemInit : Entire System Initialization
//---------------------------------------------------------------------------
void TVPSystemInit() {
#ifdef _WIN32
#ifdef USING_PROTECT
    while(!TVPProtectInit()) {
        TVPUpdateLicense();
    }
#endif
#endif

    TVPBeforeSystemInit();

    TVPInitScriptEngine();

    TVPInitTVPGL();
    //	TVPGL_C_Init();

    TVPAfterSystemInit();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPSystemUninit : System shutdown, cleanup, etc...
//---------------------------------------------------------------------------
static void TVPCauseAtExit();

bool TVPSystemUninitCalled = false;

void TVPSystemUninit() {
    if(TVPSystemUninitCalled)
        return;
    TVPSystemUninitCalled = true;

    AppendExitTrace("native: TVPSystemUninit before TVPBeforeSystemUninit");
    TVPBeforeSystemUninit();
    AppendExitTrace("native: TVPSystemUninit after TVPBeforeSystemUninit");

    AppendExitTrace("native: TVPSystemUninit before TVPUninitTVPGL");
    TVPUninitTVPGL();
    AppendExitTrace("native: TVPSystemUninit after TVPUninitTVPGL");

    try {
        AppendExitTrace("native: TVPSystemUninit before TVPUninitScriptEngine");
        TVPUninitScriptEngine();
        AppendExitTrace("native: TVPSystemUninit after TVPUninitScriptEngine");
    } catch(...) {
        AppendExitTrace("native: TVPSystemUninit TVPUninitScriptEngine threw");
        // ignore errors
    }

    AppendExitTrace("native: TVPSystemUninit before TVPAfterSystemUninit");
    TVPAfterSystemUninit();
    AppendExitTrace("native: TVPSystemUninit after TVPAfterSystemUninit");

    AppendExitTrace("native: TVPSystemUninit before TVPCauseAtExit");
    TVPCauseAtExit();
    AppendExitTrace("native: TVPSystemUninit after TVPCauseAtExit");
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPAddAtExitHandler related
//---------------------------------------------------------------------------
struct tTVPAtExitInfo {
    tTVPAtExitInfo(tjs_int pri, void (*handler)()) {
        Priority = pri, Handler = handler;
    }

    tjs_int Priority;

    void (*Handler)();

    bool operator<(const tTVPAtExitInfo &r) const {
        return this->Priority < r.Priority;
    }

    bool operator>(const tTVPAtExitInfo &r) const {
        return this->Priority > r.Priority;
    }

    bool operator==(const tTVPAtExitInfo &r) const {
        return this->Priority == r.Priority;
    }
};

static std::vector<tTVPAtExitInfo> *TVPAtExitInfos = nullptr;
static bool TVPAtExitShutdown = false;

//---------------------------------------------------------------------------
void TVPAddAtExitHandler(tjs_int pri, void (*handler)()) {
    if(TVPAtExitShutdown)
        return;

    if(!TVPAtExitInfos)
        TVPAtExitInfos = new std::vector<tTVPAtExitInfo>();
    for(const auto &info : *TVPAtExitInfos) {
        if(info.Priority == pri && info.Handler == handler)
            return;
    }
    TVPAtExitInfos->emplace_back(pri, handler);
}

//---------------------------------------------------------------------------
static void TVPCauseAtExit() {
    if(TVPAtExitShutdown)
        return;
    TVPAtExitShutdown = true;

    if(!TVPAtExitInfos)
        return;

    std::sort(TVPAtExitInfos->begin(),
              TVPAtExitInfos->end()); // descending sort

    int index = 0;
    for(auto i = TVPAtExitInfos->begin(); i != TVPAtExitInfos->end(); ++i, ++index) {
        char buffer[128] = {0};
        std::snprintf(buffer, sizeof(buffer),
                      "native: TVPCauseAtExit handler index=%d priority=%d begin",
                      index, static_cast<int>(i->Priority));
        AppendExitTrace(buffer);
        i->Handler();
        std::snprintf(buffer, sizeof(buffer),
                      "native: TVPCauseAtExit handler index=%d priority=%d end",
                      index, static_cast<int>(i->Priority));
        AppendExitTrace(buffer);
    }
}
//---------------------------------------------------------------------------

void TVPResetSystemInitStateForRestart() {
    TVPSystemUninitCalled = false;
    TVPAtExitShutdown = false;
    TVPProjectDir.Clear();
    TVPDataPath.Clear();
}
//---------------------------------------------------------------------------
