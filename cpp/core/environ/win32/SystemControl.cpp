#include "tjsCommHead.h"
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
#include <algorithm>
#include "SystemControl.h"
#include "EventIntf.h"
#include "MsgIntf.h"
// #include "WindowFormUnit.h"
#include "SysInitIntf.h"
#include "SysInitImpl.h"
#include "ScriptMgnIntf.h"
#include "WindowIntf.h"
#include "WindowImpl.h"
#include "StorageIntf.h"
#include "XP3Archive.h"
#include "GraphicsLoaderIntf.h"
#include "LayerIntf.h"
#include "EmergencyExit.h" // for TVPCPUClock
#include "DebugIntf.h"
// #include "VersionFormUnit.h"
#include "WaveImpl.h"
#include "SystemImpl.h"
#include "UserEvent.h"
#include "Application.h"
#include "Platform.h"
#include "TickCount.h"
#include "Random.h"
#include "RenderManager.h"
#include "tjsConfig.h"
#ifdef __APPLE__
#include <malloc/malloc.h>
#endif

extern "C" int64_t TJS_GetCustomObjectCount();
extern "C" int64_t TJS_GetScriptBlockCount();
extern "C" int64_t TJS_GetInterCodeContextCount();
extern "C" int64_t TJS_GetVSNetBytes();
extern "C" int64_t TJS_GetOrphanedICCCount();
extern "C" void TJS_GetScriptCacheStats(int64_t *exec_hit, int64_t *exec_miss,
                                         int64_t *exec_named,
                                         int64_t *eval_hit, int64_t *eval_miss);
extern "C" void TJS_GetObjByHashBits(int64_t out[8]);
extern "C" void TJS_GetDictStats(int64_t *created, int64_t *destroyed);
extern "C" void TJS_GetArrayStats(int64_t *created, int64_t *destroyed);

static bool (*g_GetPSBCacheInfo)(size_t &usedBytes, size_t &limitBytes) = nullptr;

void TVPRegisterPSBCacheInfoCallback(bool (*cb)(size_t &, size_t &)) {
    g_GetPSBCacheInfo = cb;
}

tTVPSystemControl *TVPSystemControl;
bool TVPSystemControlAlive = false;

//---------------------------------------------------------------------------
// Get whether to control main thread priority or to insert wait
//---------------------------------------------------------------------------
static bool TVPMainThreadPriorityControlInit = false;
static bool TVPMainThreadPriorityControl = false;
static bool TVPGetMainThreadPriorityControl() {
    if(TVPMainThreadPriorityControlInit)
        return TVPMainThreadPriorityControl;
    tTJSVariant val;
    if(TVPGetCommandLine(TJS_W("-lowpri"), &val)) {
        ttstr str(val);
        if(str == TJS_W("yes"))
            TVPMainThreadPriorityControl = true;
    }

    TVPMainThreadPriorityControlInit = true;

    return TVPMainThreadPriorityControl;
}

namespace {
tjs_int TVPClampInt(tjs_int value, tjs_int min_value, tjs_int max_value) {
    if(value < min_value)
        return min_value;
    if(value > max_value)
        return max_value;
    return value;
}

tjs_int TVPGetIntegerOption(const tjs_char *name, tjs_int fallback,
                            tjs_int min_value, tjs_int max_value) {
    tTJSVariant val;
    if(!TVPGetCommandLine(name, &val))
        return fallback;
    return TVPClampInt((tjs_int)val.AsInteger(), min_value, max_value);
}

tjs_int TVPGetSystemTotalMemoryMB() {
    TVPMemoryInfo meminfo{};
    TVPGetMemoryInfo(meminfo);
    if(meminfo.MemTotal == 0)
        return 0;
    return static_cast<tjs_int>(meminfo.MemTotal / 1024);
}

tjs_int TVPResolveBudgetMB(tjs_int configured_budget_mb) {
    if(configured_budget_mb > 0)
        return configured_budget_mb;
    const tjs_int total_mb = TVPGetSystemTotalMemoryMB();
    if(total_mb <= 0)
        return 768;
    return TVPClampInt(total_mb / 4, 512, 1024);
}
} // namespace

void tTVPSystemControl::ReloadMemoryGovernorConfig(bool force) {
    const tjs_int generation = TVPGetCommandLineArgumentGeneration();
    if(!force && generation == LastCommandLineGeneration)
        return;

    LastCommandLineGeneration = generation;

    tTJSVariant profile_value;
    ttstr profile = TJS_W("balanced");
    if(TVPGetCommandLine(TJS_W("memory_profile"), &profile_value))
        profile = ttstr(profile_value).AsLowerCase();

    if(profile == TJS_W("aggressive") || profile == TJS_W("lowmem")) {
        MemoryProfile = 1;
    } else {
        MemoryProfile = 0;
    }

    MemoryBudgetMB = TVPGetIntegerOption(TJS_W("memory_budget_mb"), 0, 0, 8192);
    MemoryLogIntervalMS =
        TVPGetIntegerOption(TJS_W("memory_log_interval_ms"), 12000, 3000, 120000);
}

void tTVPSystemControl::RunMemoryGovernor(uint32_t tick) {
    ReloadMemoryGovernorConfig(false);
    if(tick - LastMemoryGovernorTick < 1000)
        return;
    LastMemoryGovernorTick = tick;

    const tjs_int self_used_mb = TVPGetSelfUsedMemory();
    const tjs_int free_mb = TVPGetSystemFreeMemory();
    if(self_used_mb < 0)
        return;

    const tjs_int budget_mb = TVPResolveBudgetMB(MemoryBudgetMB);
    const tjs_int soft_threshold = budget_mb * (MemoryProfile ? 60 : 68) / 100;
    const tjs_int hard_threshold = budget_mb * (MemoryProfile ? 72 : 78) / 100;
    const tjs_int critical_threshold =
        budget_mb * (MemoryProfile ? 82 : 88) / 100;

    tjs_int pressure = 0;
    if(self_used_mb >= critical_threshold || (free_mb >= 0 && free_mb < 512))
        pressure = 3;
    else if(self_used_mb >= hard_threshold || (free_mb >= 0 && free_mb < 800))
        pressure = 2;
    else if(self_used_mb >= soft_threshold || (free_mb >= 0 && free_mb < 1200))
        pressure = 1;

    tjs_int heap_in_use_mb = 0;
    tjs_int heap_alloc_mb = 0;
#ifdef __APPLE__
    {
        malloc_statistics_t mst;
        malloc_zone_statistics(nullptr, &mst);
        heap_in_use_mb =
            static_cast<tjs_int>(mst.size_in_use / (1024ULL * 1024ULL));
        heap_alloc_mb =
            static_cast<tjs_int>(mst.size_allocated / (1024ULL * 1024ULL));
        if(heap_in_use_mb > budget_mb / 2 && pressure < 2)
            pressure = 2;
        if(heap_in_use_mb > budget_mb * 3 / 4 && pressure < 3)
            pressure = 3;
    }
#endif

    const tjs_int base_graphic_limit_mb =
        TVPClampInt(budget_mb / (MemoryProfile ? 10 : 12), 16,
                    MemoryProfile ? 64 : 96);
    tjs_int target_graphic_limit_mb = base_graphic_limit_mb;
    if(pressure == 1)
        target_graphic_limit_mb = TVPClampInt(base_graphic_limit_mb * 2 / 3, 24, 64);
    else if(pressure >= 2)
        target_graphic_limit_mb = TVPClampInt(base_graphic_limit_mb / 3, 24, 48);

    const tjs_uint64 target_graphic_bytes =
        static_cast<tjs_uint64>(target_graphic_limit_mb) * 1024ULL * 1024ULL;
    if(TVPGetGraphicCacheLimit() != target_graphic_bytes)
        TVPSetGraphicCacheLimit(target_graphic_bytes);

    tjs_uint archive_limit = MemoryProfile ? 48 : 128;
    if(budget_mb > 2500)
        archive_limit = 128;
    if(pressure == 1)
        archive_limit = std::max<tjs_uint>(32, archive_limit * 3 / 4);
    else if(pressure == 2)
        archive_limit = std::max<tjs_uint>(20, archive_limit / 2);
    else if(pressure >= 3)
        archive_limit = 12;
    TVPSetArchiveCacheCount(archive_limit);

    tjs_uint auto_path_limit = 256;
    if(pressure == 1)
        auto_path_limit = 192;
    else if(pressure == 2)
        auto_path_limit = 128;
    else if(pressure >= 3)
        auto_path_limit = 96;
    TVPSetAutoPathCacheMaxCount(auto_path_limit);

    TVPFreeUnusedLayerCache = (MemoryProfile == 1 || pressure >= 1);

    {
        unsigned int dc = 0;
        uint64_t vs = 0;
        if(TVPGetRenderManager()->GetRenderStat(dc, vs)) {
            const tjs_int vmem_mb_now =
                static_cast<tjs_int>(vs / (1024ULL * 1024ULL));
            const tjs_int vmem_budget_mb = MemoryProfile ? 128 : 256;
            if(vmem_mb_now > vmem_budget_mb && pressure < 1)
                pressure = 1;
        }
    }

    const uint32_t deep_compact_interval =
        static_cast<uint32_t>(MemoryProfile ? 6000 : 10000);
    const uint32_t aggressive_compact_interval =
        static_cast<uint32_t>(MemoryProfile ? 10000 : 16000);

    const uint32_t idle_compact_interval =
        static_cast<uint32_t>(MemoryProfile ? 20000 : 40000);
    if(pressure == 0 && tick - LastIdleCompactTick >= idle_compact_interval) {
        LastIdleCompactTick = tick;
        TVPDeliverCompactEvent(TVP_COMPACT_LEVEL_DEACTIVATE);
#ifdef __APPLE__
        malloc_zone_pressure_relief(nullptr, 0);
#endif
    }

    if(pressure >= 1 && tick - LastDeepCompactedTick >= deep_compact_interval) {
        LastDeepCompactedTick = tick;
        TVPDeliverCompactEvent(TVP_COMPACT_LEVEL_MINIMIZE);
#ifdef __APPLE__
        malloc_zone_pressure_relief(nullptr, 0);
#endif
    }

    if(pressure >= 2 &&
       tick - LastAggressiveCompactedTick >= aggressive_compact_interval) {
        LastAggressiveCompactedTick = tick;
        TVPClearXP3SegmentCache();
        TVPDeliverCompactEvent(TVP_COMPACT_LEVEL_MINIMIZE);
#ifdef __APPLE__
        malloc_zone_pressure_relief(nullptr, 0);
#endif
    }

    if(pressure >= 3 && tick - LastCompactedTick >= 3000) {
        LastCompactedTick = tick;
        TVPClearXP3SegmentCache();
        TVPDeliverCompactEvent(TVP_COMPACT_LEVEL_MAX);
#ifdef __APPLE__
        malloc_zone_pressure_relief(nullptr, 0);
#endif
    }

#ifdef __APPLE__
    {
        const tjs_int heap_ceiling_mb = budget_mb * 45 / 100;
        if(heap_in_use_mb > heap_ceiling_mb &&
           tick - LastHeapCeilingTick >= 30000) {
            LastHeapCeilingTick = tick;
            spdlog::warn("Heap ceiling triggered: {}MB > {}MB ceiling, "
                         "forcing full cleanup",
                         heap_in_use_mb, heap_ceiling_mb);
            TVPClearXP3SegmentCache();
            TVPDeliverCompactEvent(TVP_COMPACT_LEVEL_MAX);
            malloc_zone_pressure_relief(nullptr, 0);
        }
    }
#endif

    static int sMemLogCount = 0;
    const uint32_t effectiveLogInterval =
        (sMemLogCount < 30) ? 3000 : static_cast<uint32_t>(MemoryLogIntervalMS);
    if(tick - LastMemoryLogTick >= effectiveLogInterval) {
        LastMemoryLogTick = tick;
        sMemLogCount++;
        const tjs_int graphic_used_mb = static_cast<tjs_int>(
            TVPGetGraphicCacheTotalBytes() / (1024ULL * 1024ULL));
        const tjs_int graphic_limit_mb = static_cast<tjs_int>(
            TVPGetGraphicCacheLimit() / (1024ULL * 1024ULL));
        const tjs_int xp3_seg_mb = static_cast<tjs_int>(
            TVPGetXP3SegmentCacheTotalBytes() / (1024ULL * 1024ULL));

        tjs_int psb_used_mb = 0, psb_limit_mb = 0;
        if(g_GetPSBCacheInfo) {
            size_t usedBytes = 0, limitBytes = 0;
            if(g_GetPSBCacheInfo(usedBytes, limitBytes)) {
                psb_used_mb = static_cast<tjs_int>(usedBytes / (1024ULL * 1024ULL));
                psb_limit_mb = static_cast<tjs_int>(limitBytes / (1024ULL * 1024ULL));
            }
        }

        tjs_int vmem_mb = 0;
        unsigned int drawCount = 0;
        uint64_t vmemsize = 0;
        if(TVPGetRenderManager()->GetRenderStat(drawCount, vmemsize))
            vmem_mb = static_cast<tjs_int>(vmemsize / (1024ULL * 1024ULL));

        const tjs_int layer_count = TVPGetLayerCount();
        const tjs_int layermem_mb = static_cast<tjs_int>(
            TVPGetLayerTotalBitmapBytes() / (1024ULL * 1024ULL));

        int64_t tjsNetBytes = 0, tjsAllocCount = 0, tjsFreeCount = 0;
        TJS_GetMallocStats(tjsNetBytes, tjsAllocCount, tjsFreeCount);
        const tjs_int tjs_net_mb = static_cast<tjs_int>(tjsNetBytes / (1024LL * 1024LL));

        const tjs_int tracked_mb = graphic_used_mb + psb_used_mb + vmem_mb +
                                    xp3_seg_mb + layermem_mb;
        const tjs_int untracked_mb = self_used_mb - tracked_mb;

        const tjs_int obj_count = static_cast<tjs_int>(TJS_GetCustomObjectCount());
        const tjs_int sb_count = static_cast<tjs_int>(TJS_GetScriptBlockCount());
        const tjs_int icc_count = static_cast<tjs_int>(TJS_GetInterCodeContextCount());
        const tjs_int orphan_icc = static_cast<tjs_int>(TJS_GetOrphanedICCCount());
        const tjs_int vs_mb = static_cast<tjs_int>(TJS_GetVSNetBytes() / (1024LL * 1024LL));

        int64_t exec_hit=0, exec_miss=0, exec_named=0, eval_hit=0, eval_miss=0;
        TJS_GetScriptCacheStats(&exec_hit, &exec_miss, &exec_named,
                                &eval_hit, &eval_miss);

        static tjs_int sPrevHeapMB = 0, sPrevObjCount = 0, sPrevSBCount = 0;
        static tjs_int sPrevVsMB = 0, sPrevTjsMB = 0, sPrevUsedMB = 0;
        static tjs_int sPrevIccCount = 0;

        const tjs_int d_heap = heap_in_use_mb - sPrevHeapMB;
        const tjs_int d_obj = obj_count - sPrevObjCount;
        const tjs_int d_sb = sb_count - sPrevSBCount;
        const tjs_int d_icc = icc_count - sPrevIccCount;
        const tjs_int d_vs = vs_mb - sPrevVsMB;
        const tjs_int d_tjs = tjs_net_mb - sPrevTjsMB;
        const tjs_int d_used = self_used_mb - sPrevUsedMB;

        sPrevHeapMB = heap_in_use_mb;
        sPrevObjCount = obj_count;
        sPrevSBCount = sb_count;
        sPrevIccCount = icc_count;
        sPrevVsMB = vs_mb;
        sPrevTjsMB = tjs_net_mb;
        sPrevUsedMB = self_used_mb;

        TVPAddLog(ttstr(TJS_W("(mem) used=")) + ttstr(self_used_mb) +
                  TJS_W("MB free=") + ttstr(free_mb) + TJS_W("MB budget=") +
                  ttstr(budget_mb) + TJS_W("MB pressure=") + ttstr(pressure) +
                  TJS_W(" gcache=") + ttstr(graphic_used_mb) + TJS_W("/") +
                  ttstr(graphic_limit_mb) + TJS_W("MB psbcache=") +
                  ttstr(psb_used_mb) + TJS_W("/") + ttstr(psb_limit_mb) +
                  TJS_W("MB vmem=") + ttstr(vmem_mb) +
                  TJS_W("MB xp3seg=") + ttstr(xp3_seg_mb) +
                  TJS_W("MB layers=") + ttstr(layer_count) +
                  TJS_W("/") + ttstr(layermem_mb) +
                  TJS_W("MB heap=") + ttstr(heap_in_use_mb) + TJS_W("/") +
                  ttstr(heap_alloc_mb) +
                  TJS_W("MB tjs=") + ttstr(tjs_net_mb) +
                  TJS_W("MB vs=") + ttstr(vs_mb) +
                  TJS_W("MB obj=") + ttstr(obj_count) +
                  TJS_W(" sb=") + ttstr(sb_count) +
                  TJS_W(" icc=") + ttstr(icc_count) +
                  TJS_W(" orphan=") + ttstr(orphan_icc) +
                  TJS_W(" untracked=") +
                  ttstr(untracked_mb) + TJS_W("MB"));

        TVPAddLog(ttstr(TJS_W("(mem_delta) d_used=")) + ttstr(d_used) +
                  TJS_W("MB d_heap=") + ttstr(d_heap) +
                  TJS_W("MB d_tjs=") + ttstr(d_tjs) +
                  TJS_W("MB d_vs=") + ttstr(d_vs) +
                  TJS_W("MB d_obj=") + ttstr(d_obj) +
                  TJS_W(" d_sb=") + ttstr(d_sb) +
                  TJS_W(" d_icc=") + ttstr(d_icc) +
                  TJS_W(" cache:exec_hit=") + ttstr((tjs_int)exec_hit) +
                  TJS_W(" exec_miss=") + ttstr((tjs_int)exec_miss) +
                  TJS_W(" exec_named=") + ttstr((tjs_int)exec_named) +
                  TJS_W(" eval_hit=") + ttstr((tjs_int)eval_hit) +
                  TJS_W(" eval_miss=") + ttstr((tjs_int)eval_miss));

        int64_t obh[8];
        TJS_GetObjByHashBits(obh);
        int64_t dict_c=0, dict_d=0, arr_c=0, arr_d=0;
        TJS_GetDictStats(&dict_c, &dict_d);
        TJS_GetArrayStats(&arr_c, &arr_d);
        TVPAddLog(ttstr(TJS_W("(obj_detail) h0=")) + ttstr((tjs_int)obh[0]) +
                  TJS_W(" h1=") + ttstr((tjs_int)obh[1]) +
                  TJS_W(" h2=") + ttstr((tjs_int)obh[2]) +
                  TJS_W(" h3=") + ttstr((tjs_int)obh[3]) +
                  TJS_W(" h4=") + ttstr((tjs_int)obh[4]) +
                  TJS_W(" h5+") + ttstr((tjs_int)(obh[5]+obh[6]+obh[7])) +
                  TJS_W(" dict=") + ttstr((tjs_int)(dict_c-dict_d)) +
                  TJS_W("(+") + ttstr((tjs_int)dict_c) +
                  TJS_W("/-") + ttstr((tjs_int)dict_d) +
                  TJS_W(") arr=") + ttstr((tjs_int)(arr_c-arr_d)) +
                  TJS_W("(+") + ttstr((tjs_int)arr_c) +
                  TJS_W("/-") + ttstr((tjs_int)arr_d) +
                  TJS_W(")"));
    }
}

tTVPSystemControl::tTVPSystemControl() : EventEnable(true) {
    ContinuousEventCalling = false;
    AutoShowConsoleOnError = false;

    LastCompactedTick = 0;
    LastDeepCompactedTick = 0;
    LastAggressiveCompactedTick = 0;
    LastIdleCompactTick = 0;
    LastMemoryGovernorTick = 0;
    LastMemoryLogTick = 0;
    LastCommandLineGeneration = -1;
    MemoryProfile = 0;
    MemoryBudgetMB = 0;
    MemoryLogIntervalMS = 12000;
    LastCloseClickedTick = 0;
    LastShowModalWindowSentTick = 0;
    LastRehashedTick = 0;

    ReloadMemoryGovernorConfig(true);

    TVPSystemControlAlive = true;
#if 0
	SystemWatchTimer.SetInterval(50);
	SystemWatchTimer.SetOnTimerHandler( this, &tTVPSystemControl::SystemWatchTimerTimer );
	SystemWatchTimer.SetEnabled( true );
#endif
}
void tTVPSystemControl::InvokeEvents() { CallDeliverAllEventsOnIdle(); }
void tTVPSystemControl::CallDeliverAllEventsOnIdle() {
    //	Application->PostMessageToMainWindow(
    // TVP_EV_DELIVER_EVENTS_DUMMY, 0, 0
    //);
}

void tTVPSystemControl::BeginContinuousEvent() {
    if(!ContinuousEventCalling) {
        ContinuousEventCalling = true;
        InvokeEvents();
        if(TVPGetMainThreadPriorityControl()) {
            // make main thread priority lower
            //			SetThreadPriority(GetCurrentThread(),
            // THREAD_PRIORITY_LOWEST);
        }
    }
}
void tTVPSystemControl::EndContinuousEvent() {
    if(ContinuousEventCalling) {
        ContinuousEventCalling = false;
        if(TVPGetMainThreadPriorityControl()) {
            // make main thread priority normal
            //			SetThreadPriority(GetCurrentThread(),
            // THREAD_PRIORITY_NORMAL);
        }
    }
}
//---------------------------------------------------------------------------
void tTVPSystemControl::NotifyCloseClicked() {
    // close Button is clicked
    LastCloseClickedTick = TVPGetRoughTickCount32();
}

void tTVPSystemControl::NotifyEventDelivered() {
    // called from event system, notifying the event is delivered.
    LastCloseClickedTick = 0;
    // if(TVPHaltWarnForm) delete TVPHaltWarnForm, TVPHaltWarnForm =
    // nullptr;
}

bool tTVPSystemControl::ApplicationIdle() {
    DeliverEvents();
    bool cont = !ContinuousEventCalling;
    MixedIdleTick += TVPGetRoughTickCount32();
    return cont;
}

void tTVPSystemControl::DeliverEvents() {
    if(TVPTerminated)
        return;
    if(ContinuousEventCalling)
        TVPProcessContinuousHandlerEventFlag = true; // set flag

    if(EventEnable) {
        TVPDeliverAllEvents();
    }
}

void tTVPSystemControl::SystemWatchTimerTimer() {
    if(TVPTerminated) {
        // this will ensure terminating the application.
        // the WM_QUIT message disappears in some unknown
        // situations...
        //		Application->PostMessageToMainWindow(
        // TVP_EV_DELIVER_EVENTS_DUMMY, 0, 0 );
        Application->Terminate();
        //		Application->PostMessageToMainWindow(
        // TVP_EV_DELIVER_EVENTS_DUMMY, 0, 0 );
        return;
    }

    // call events
    uint32_t tick = TVPGetRoughTickCount32();
    // push environ noise
    TVPPushEnvironNoise(&tick, sizeof(tick));
    TVPPushEnvironNoise(&LastCompactedTick, sizeof(LastCompactedTick));
    TVPPushEnvironNoise(&LastShowModalWindowSentTick,
                        sizeof(LastShowModalWindowSentTick));
    TVPPushEnvironNoise(&MixedIdleTick, sizeof(MixedIdleTick));
#if 0
	POINT pt;
	::GetCursorPos(&pt);
	TVPPushEnvironNoise(&pt, sizeof(pt));

	// CPU clock monitoring
	{
		static bool clock_rough_printed = false;
		if( !clock_rough_printed && TVPCPUClockAccuracy == ccaRough ) {
			tjs_char msg[80];
			TJS_snprintf(msg, 80, TVPInfoCpuClockRoughly, (int)TVPCPUClock);
			TVPAddImportantLog(msg);
			clock_rough_printed = true;
		}
		static bool clock_printed = false;
		if( !clock_printed && TVPCPUClockAccuracy == ccaAccurate ) {
			tjs_char msg[80];
			TJS_snprintf(msg, 80, TVPInfoCpuClock, (float)TVPCPUClock);
			TVPAddImportantLog(msg);
			clock_printed = true;
		}
	}
#endif
    // check status and deliver events
    DeliverEvents();
    if(TVPTerminated)
        return;

    // call TickBeat
    tjs_int count = TVPGetWindowCount();
    for(tjs_int i = 0; i < count; i++) {
        tTJSNI_Window *win = TVPGetWindowListAt(i);
        win->TickBeat();
    }

    if(!ContinuousEventCalling && tick - LastCompactedTick > 4000) {
        // idle state over 4 sec.
        LastCompactedTick = tick;

        // fire compact event
        TVPDeliverCompactEvent(TVP_COMPACT_LEVEL_IDLE);
    } else if(ContinuousEventCalling && tick - LastCompactedTick > 10000) {
        LastCompactedTick = tick;
        TVPDeliverCompactEvent(TVP_COMPACT_LEVEL_IDLE);
    }

    RunMemoryGovernor(tick);

    if(!ContinuousEventCalling && tick > LastRehashedTick + 1500) {
        // TJS2 object rehash
        LastRehashedTick = tick;
        TJSDoRehash();
    } else if(ContinuousEventCalling && tick > LastRehashedTick + 10000) {
        // Periodically rehash TJS2 object tables even during continuous
        // events to prevent hash table fragmentation and memory waste.
        LastRehashedTick = tick;
        TJSDoRehash();
    }
    // ensure modal window visible
    if(tick > LastShowModalWindowSentTick + 4100) {
        //	::PostMessage(Handle, WM_USER+0x32, 0, 0);
        // This is currently disabled because IME composition window
        // hides behind the window which is bringed top by the
        // window-rearrangement.
        LastShowModalWindowSentTick = tick;
    }
}
