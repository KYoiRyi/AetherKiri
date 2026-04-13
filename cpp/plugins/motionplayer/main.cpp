//
// motionplayer.dll — NCB TJS2 binding registration
// Full EmotePlayer engine: 3-phase animation pipeline with mesh deformation,
// spring physics, particle system, and Layer-API rendering.
//
#include <spdlog/spdlog.h>
#include "tjs.h"
#include "tjsDictionary.h"
#include "ncbind.hpp"
#include "psbfile/PSBFile.h"
#include "base/ScriptMgnIntf.h"

#include "ResourceManager.h"
#include "EmotePlayer.h"
#include "Player.h"
#include "SeparateLayerAdaptor.h"

using namespace motion;
using namespace TJS;

#define NCB_MODULE_NAME TJS_W("motionplayer.dll")
#define LOGGER spdlog::get("plugin")

// ============================================================================
// SeparateLayerAdaptor — Layer indirection wrapper
// ============================================================================

static motion::SeparateLayerAdaptor *GetSeparateLayerAdaptorInstance(iTJSDispatch2 *objthis) {
    return ncbInstanceAdaptor<motion::SeparateLayerAdaptor>::GetNativeInstance(objthis);
}

static iTJSDispatch2 *GetSeparateAdaptorRenderTarget(motion::SeparateLayerAdaptor *adaptor);

class GenericMockObject;
static tjs_error Universal_missing_method(tTJSVariant *result, tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis);

iTJSDispatch2 *ResolveLayerTreeOwnerBase(iTJSDispatch2 *base) {
    if(!base) return nullptr;
    auto *adaptor = ncbInstanceAdaptor<motion::SeparateLayerAdaptor>::GetNativeInstance(base);
    if(adaptor) return GetSeparateAdaptorRenderTarget(adaptor);
    return base;
}

static iTJSDispatch2 *GetSeparateAdaptorRenderTarget(motion::SeparateLayerAdaptor *adaptor) {
    if(!adaptor) return nullptr;
    if(adaptor->getTarget()) return adaptor->getTarget();

    auto *owner = adaptor->getOwner();
    if(!owner) return nullptr;

    tTJSVariant windowVar;
    iTJSDispatch2 *windowObj = owner;
    if(TJS_SUCCEEDED(owner->PropGet(0, TJS_W("window"), nullptr, &windowVar, owner)) &&
       windowVar.Type() == tvtObject && windowVar.AsObjectNoAddRef()) {
        windowObj = windowVar.AsObjectNoAddRef();
    }

    tTJSVariant parentVar;
    if(TJS_FAILED(owner->PropGet(0, TJS_W("primaryLayer"), nullptr, &parentVar, owner)) ||
       parentVar.Type() != tvtObject || !parentVar.AsObjectNoAddRef()) {
        if(TJS_FAILED(windowObj->PropGet(0, TJS_W("primaryLayer"), nullptr, &parentVar, windowObj)) ||
           parentVar.Type() != tvtObject || !parentVar.AsObjectNoAddRef()) {
            return owner;
        }
    }

    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(!global) return owner;

    tTJSVariant layerClassVar;
    if(TJS_FAILED(global->PropGet(0, TJS_W("Layer"), nullptr, &layerClassVar, global)) ||
       layerClassVar.Type() != tvtObject || !layerClassVar.AsObjectNoAddRef()) {
        global->Release();
        return owner;
    }

    iTJSDispatch2 *layerClass = layerClassVar.AsObjectNoAddRef();
    tTJSVariant args[2] = { tTJSVariant(windowObj, windowObj),
                            tTJSVariant(parentVar.AsObjectNoAddRef(), parentVar.AsObjectNoAddRef()) };
    tTJSVariant *argv[] = { &args[0], &args[1] };
    iTJSDispatch2 *layerObj = nullptr;
    const auto hr = layerClass->CreateNew(0, nullptr, nullptr, &layerObj, 2, argv, layerClass);
    global->Release();
    if(TJS_FAILED(hr) || !layerObj) {
        return owner;
    }

    auto syncProp = [&](const tjs_char *name) {
        tTJSVariant value;
        if(TJS_SUCCEEDED(owner->PropGet(0, name, nullptr, &value, owner))) {
            layerObj->PropSet(TJS_MEMBERENSURE, name, nullptr, &value, layerObj);
        }
    };
    syncProp(TJS_W("left"));
    syncProp(TJS_W("top"));
    syncProp(TJS_W("width"));
    syncProp(TJS_W("height"));
    syncProp(TJS_W("visible"));
    syncProp(TJS_W("opacity"));
    syncProp(TJS_W("name"));

    tTJSVariant htVal(static_cast<tjs_int>(256));
    layerObj->PropSet(TJS_MEMBERENSURE, TJS_W("hitThreshold"), nullptr, &htVal, layerObj);

    tTJSVariant ownerHtVal(static_cast<tjs_int>(0));
    owner->PropSet(TJS_MEMBERENSURE, TJS_W("hitThreshold"), nullptr, &ownerHtVal, owner);

    adaptor->setTarget(layerObj);
    layerObj->Release();
    return adaptor->getTarget() ? adaptor->getTarget() : owner;
}

static tjs_error SeparateLayerAdaptor_getWidth(tTJSVariant *r, tjs_int, tTJSVariant **,
                                               iTJSDispatch2 *objthis) {
    auto *adaptor = GetSeparateLayerAdaptorInstance(objthis);
    auto *target = GetSeparateAdaptorRenderTarget(adaptor);
    if(!target) { if(r) *r = tTJSVariant(static_cast<tjs_int>(0)); return TJS_S_OK; }
    tTJSVariant value;
    const auto hr = target->PropGet(0, TJS_W("width"), nullptr, &value, target);
    if(r) *r = TJS_SUCCEEDED(hr) ? value : tTJSVariant(static_cast<tjs_int>(0));
    return TJS_S_OK;
}
static tjs_error SeparateLayerAdaptor_getHeight(tTJSVariant *r, tjs_int, tTJSVariant **,
                                                iTJSDispatch2 *objthis) {
    auto *adaptor = GetSeparateLayerAdaptorInstance(objthis);
    auto *target = GetSeparateAdaptorRenderTarget(adaptor);
    if(!target) { if(r) *r = tTJSVariant(static_cast<tjs_int>(0)); return TJS_S_OK; }
    tTJSVariant value;
    const auto hr = target->PropGet(0, TJS_W("height"), nullptr, &value, target);
    if(r) *r = TJS_SUCCEEDED(hr) ? value : tTJSVariant(static_cast<tjs_int>(0));
    return TJS_S_OK;
}
static tjs_error SeparateLayerAdaptor_loadImages(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                                                 iTJSDispatch2 *objthis) {
    auto *adaptor = GetSeparateLayerAdaptorInstance(objthis);
    auto *target = GetSeparateAdaptorRenderTarget(adaptor);
    if(!target || count < 1) return TJS_E_INVALIDPARAM;
    return target->FuncCall(0, TJS_W("loadImages"), nullptr, r, count, p, target);
}
static tjs_error SeparateLayerAdaptor_fillRect(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                                               iTJSDispatch2 *objthis) {
    auto *adaptor = GetSeparateLayerAdaptorInstance(objthis);
    auto *target = GetSeparateAdaptorRenderTarget(adaptor);
    if(!target || count < 5) return TJS_E_INVALIDPARAM;
    return target->FuncCall(0, TJS_W("fillRect"), nullptr, r, count, p, target);
}
static tjs_error SeparateLayerAdaptor_operateRect(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                                                  iTJSDispatch2 *objthis) {
    auto *adaptor = GetSeparateLayerAdaptorInstance(objthis);
    auto *target = GetSeparateAdaptorRenderTarget(adaptor);
    if(!target || count < 9) return TJS_E_INVALIDPARAM;
    return target->FuncCall(0, TJS_W("operateRect"), nullptr, r, count, p, target);
}
static tjs_error SeparateLayerAdaptor_getFace(tTJSVariant *r, tjs_int, tTJSVariant **,
                                              iTJSDispatch2 *objthis) {
    auto *adaptor = GetSeparateLayerAdaptorInstance(objthis);
    auto *target = GetSeparateAdaptorRenderTarget(adaptor);
    if(!target) { if(r) *r = tTJSVariant(static_cast<tjs_int>(0)); return TJS_S_OK; }
    tTJSVariant value;
    const auto hr = target->PropGet(0, TJS_W("face"), nullptr, &value, target);
    if(r) *r = TJS_SUCCEEDED(hr) ? value : tTJSVariant(static_cast<tjs_int>(0));
    return TJS_S_OK;
}
static tjs_error SeparateLayerAdaptor_setFace(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                                              iTJSDispatch2 *objthis) {
    auto *adaptor = GetSeparateLayerAdaptorInstance(objthis);
    auto *target = GetSeparateAdaptorRenderTarget(adaptor);
    if(!target || count < 1) return TJS_E_INVALIDPARAM;
    return target->PropSet(TJS_MEMBERENSURE, TJS_W("face"), nullptr, p[0], target);
}
static tjs_error SeparateLayerAdaptor_getImageWidth(tTJSVariant *r, tjs_int, tTJSVariant **,
                                                    iTJSDispatch2 *objthis) {
    auto *adaptor = GetSeparateLayerAdaptorInstance(objthis);
    auto *target = GetSeparateAdaptorRenderTarget(adaptor);
    if(!target) { if(r) *r = tTJSVariant(static_cast<tjs_int>(0)); return TJS_S_OK; }
    tTJSVariant value;
    const auto hr = target->PropGet(0, TJS_W("imageWidth"), nullptr, &value, target);
    if(r) *r = TJS_SUCCEEDED(hr) ? value : tTJSVariant(static_cast<tjs_int>(0));
    return TJS_S_OK;
}
static tjs_error SeparateLayerAdaptor_getImageHeight(tTJSVariant *r, tjs_int, tTJSVariant **,
                                                     iTJSDispatch2 *objthis) {
    auto *adaptor = GetSeparateLayerAdaptorInstance(objthis);
    auto *target = GetSeparateAdaptorRenderTarget(adaptor);
    if(!target) { if(r) *r = tTJSVariant(static_cast<tjs_int>(0)); return TJS_S_OK; }
    tTJSVariant value;
    const auto hr = target->PropGet(0, TJS_W("imageHeight"), nullptr, &value, target);
    if(r) *r = TJS_SUCCEEDED(hr) ? value : tTJSVariant(static_cast<tjs_int>(0));
    return TJS_S_OK;
}

NCB_REGISTER_SUBCLASS_DELAY(SeparateLayerAdaptor) {
    NCB_CONSTRUCTOR((iTJSDispatch2 *));
    NCB_PROPERTY_RAW_CALLBACK_RO(width, SeparateLayerAdaptor_getWidth, 0);
    NCB_PROPERTY_RAW_CALLBACK_RO(height, SeparateLayerAdaptor_getHeight, 0);
    NCB_PROPERTY_RAW_CALLBACK(face, SeparateLayerAdaptor_getFace, SeparateLayerAdaptor_setFace, 0);
    NCB_PROPERTY_RAW_CALLBACK_RO(imageWidth, SeparateLayerAdaptor_getImageWidth, 0);
    NCB_PROPERTY_RAW_CALLBACK_RO(imageHeight, SeparateLayerAdaptor_getImageHeight, 0);
    NCB_METHOD_RAW_CALLBACK(loadImages, SeparateLayerAdaptor_loadImages, 0);
    NCB_METHOD_RAW_CALLBACK(fillRect, SeparateLayerAdaptor_fillRect, 0);
    NCB_METHOD_RAW_CALLBACK(operateRect, SeparateLayerAdaptor_operateRect, 0);
}

// ============================================================================
// Player — raw callback wrappers for TJS binding
// ============================================================================

static motion::Player *GetPlayerInstance(iTJSDispatch2 *objthis) {
    return ncbInstanceAdaptor<motion::Player>::GetNativeInstance(objthis);
}

// --- Property raw callbacks ---

static tjs_error Player_getPlaying(tTJSVariant *r, tjs_int, tTJSVariant **,
                                   iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getAllplaying() : false);
    return TJS_S_OK;
}
static tjs_error Player_getAllplaying(tTJSVariant *r, tjs_int, tTJSVariant **,
                                      iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getAllplaying() : false);
    return TJS_S_OK;
}
static tjs_error Player_getChara(tTJSVariant *r, tjs_int, tTJSVariant **,
                                 iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getChara() : ttstr());
    return TJS_S_OK;
}
static tjs_error Player_setChara(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                 iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setChara(ttstr(*p[0]));
    return TJS_S_OK;
}
static tjs_error Player_getMotionKey(tTJSVariant *r, tjs_int, tTJSVariant **,
                                     iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getMotionKey() : ttstr());
    return TJS_S_OK;
}
static tjs_error Player_setMotionKey(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                     iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setMotionKey(ttstr(*p[0]));
    return TJS_S_OK;
}
static tjs_error Player_getOutline(tTJSVariant *r, tjs_int, tTJSVariant **,
                                   iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getOutline() : ttstr());
    return TJS_S_OK;
}
static tjs_error Player_setOutline(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                   iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setOutline(ttstr(*p[0]));
    return TJS_S_OK;
}
static tjs_error Player_getPriorDraw(tTJSVariant *r, tjs_int, tTJSVariant **,
                                     iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getPriorDraw() : 0.0);
    return TJS_S_OK;
}
static tjs_error Player_setPriorDraw(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                     iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setPriorDraw(p[0]->AsReal());
    return TJS_S_OK;
}
static tjs_error Player_getFrameLastTime(tTJSVariant *r, tjs_int, tTJSVariant **,
                                         iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getFrameLastTime() : 0.0);
    return TJS_S_OK;
}
static tjs_error Player_setFrameLastTime(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                         iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setFrameLastTime(p[0]->AsReal());
    return TJS_S_OK;
}
static tjs_error Player_getFrameLoopTime(tTJSVariant *r, tjs_int, tTJSVariant **,
                                         iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getFrameLoopTime() : 0.0);
    return TJS_S_OK;
}
static tjs_error Player_setFrameLoopTime(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                         iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setFrameLoopTime(p[0]->AsReal());
    return TJS_S_OK;
}
static tjs_error Player_getLoopTime(tTJSVariant *r, tjs_int, tTJSVariant **,
                                    iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getLoopTime() : 0.0);
    return TJS_S_OK;
}
static tjs_error Player_setLoopTime(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                    iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setLoopTime(p[0]->AsReal());
    return TJS_S_OK;
}
static tjs_error Player_getProcessedMeshVerticesNum(tTJSVariant *r, tjs_int, tTJSVariant **,
                                                    iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(static_cast<tjs_int>(p ? p->getProcessedMeshVerticesNum() : 0));
    return TJS_S_OK;
}
static tjs_error Player_setProcessedMeshVerticesNum(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                                    iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setProcessedMeshVerticesNum(static_cast<int>(p[0]->AsInteger()));
    return TJS_S_OK;
}
static tjs_error Player_getQueuing(tTJSVariant *r, tjs_int, tTJSVariant **,
                                   iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getQueuing() : false);
    return TJS_S_OK;
}
static tjs_error Player_setQueuing(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                   iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setQueuing(static_cast<bool>(p[0]->AsInteger()));
    return TJS_S_OK;
}
static tjs_error Player_getDirectEdit(tTJSVariant *r, tjs_int, tTJSVariant **,
                                      iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getDirectEdit() : false);
    return TJS_S_OK;
}
static tjs_error Player_setDirectEdit(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                      iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setDirectEdit(static_cast<bool>(p[0]->AsInteger()));
    return TJS_S_OK;
}
static tjs_error Player_getSelectorEnabled(tTJSVariant *r, tjs_int, tTJSVariant **,
                                           iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getSelectorEnabled() : false);
    return TJS_S_OK;
}
static tjs_error Player_setSelectorEnabled(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                           iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setSelectorEnabled(static_cast<bool>(p[0]->AsInteger()));
    return TJS_S_OK;
}
static tjs_error Player_getVariableKeys(tTJSVariant *r, tjs_int, tTJSVariant **,
                                        iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(!r) return TJS_S_OK;
    if(!p) { *r = tTJSVariant(); return TJS_S_OK; }
    tTJSVariant v = p->getVariableKeys();
    *r = v;
    return TJS_S_OK;
}
static tjs_error Player_getSyncWaiting(tTJSVariant *r, tjs_int, tTJSVariant **,
                                       iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getSyncWaiting() : false);
    return TJS_S_OK;
}
static tjs_error Player_getSyncActive(tTJSVariant *r, tjs_int, tTJSVariant **,
                                      iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getSyncActive() : false);
    return TJS_S_OK;
}
static tjs_error Player_getHasCamera(tTJSVariant *r, tjs_int, tTJSVariant **,
                                     iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getHasCamera() : false);
    return TJS_S_OK;
}
static tjs_error Player_getCameraActive(tTJSVariant *r, tjs_int, tTJSVariant **,
                                        iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getCameraActive() : false);
    return TJS_S_OK;
}
static tjs_error Player_getStereovisionActive(tTJSVariant *r, tjs_int, tTJSVariant **,
                                              iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getStereovisionActive() : false);
    return TJS_S_OK;
}
static tjs_error Player_getTickCount(tTJSVariant *r, tjs_int, tTJSVariant **,
                                     iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getTickCount() : 0.0);
    return TJS_S_OK;
}
static tjs_error Player_setTickCount(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                     iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setTickCount(p[0]->AsReal());
    return TJS_S_OK;
}
static tjs_error Player_getSpeed(tTJSVariant *r, tjs_int, tTJSVariant **,
                                 iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getSpeed() : true);
    return TJS_S_OK;
}
static tjs_error Player_setSpeed(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                 iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setSpeed(static_cast<bool>(p[0]->AsInteger()));
    return TJS_S_OK;
}
static tjs_error Player_getFrameTickCount(tTJSVariant *r, tjs_int, tTJSVariant **,
                                          iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getFrameTickCount() : 0.0);
    return TJS_S_OK;
}
static tjs_error Player_setFrameTickCount(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                          iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setFrameTickCount(p[0]->AsReal());
    return TJS_S_OK;
}
static tjs_error Player_getColorWeight(tTJSVariant *r, tjs_int, tTJSVariant **,
                                       iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getColorWeight() : 0);
    return TJS_S_OK;
}
static tjs_error Player_setColorWeight(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                       iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setColorWeight(static_cast<tjs_int>(p[0]->AsInteger()));
    return TJS_S_OK;
}
static tjs_error Player_getMaskMode(tTJSVariant *r, tjs_int, tTJSVariant **,
                                    iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getMaskMode() : 0);
    return TJS_S_OK;
}
static tjs_error Player_setMaskMode(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                    iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setMaskMode(static_cast<tjs_int>(p[0]->AsInteger()));
    return TJS_S_OK;
}
static tjs_error Player_getIndependentLayerInherit(tTJSVariant *r, tjs_int, tTJSVariant **,
                                                   iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getIndependentLayerInherit() : false);
    return TJS_S_OK;
}
static tjs_error Player_setIndependentLayerInherit(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                                   iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setIndependentLayerInherit(static_cast<bool>(p[0]->AsInteger()));
    return TJS_S_OK;
}
static tjs_error Player_getZFactor(tTJSVariant *r, tjs_int, tTJSVariant **,
                                   iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getZFactor() : 1.0);
    return TJS_S_OK;
}
static tjs_error Player_setZFactor(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                   iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setZFactor(p[0]->AsReal());
    return TJS_S_OK;
}
static tjs_error Player_getCameraTarget(tTJSVariant *r, tjs_int, tTJSVariant **,
                                        iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = p ? p->getCameraTarget() : tTJSVariant();
    return TJS_S_OK;
}
static tjs_error Player_setCameraTarget(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                        iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setCameraTarget(*p[0]);
    return TJS_S_OK;
}
static tjs_error Player_getCameraPosition(tTJSVariant *r, tjs_int, tTJSVariant **,
                                          iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = p ? p->getCameraPosition() : tTJSVariant();
    return TJS_S_OK;
}
static tjs_error Player_setCameraPosition(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                          iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setCameraPosition(*p[0]);
    return TJS_S_OK;
}
static tjs_error Player_getCameraFOV(tTJSVariant *r, tjs_int, tTJSVariant **,
                                     iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getCameraFOV() : 60.0);
    return TJS_S_OK;
}
static tjs_error Player_setCameraFOV(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                     iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setCameraFOV(p[0]->AsReal());
    return TJS_S_OK;
}
static tjs_error Player_getCameraAlive(tTJSVariant *r, tjs_int, tTJSVariant **,
                                       iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getCameraAlive() : false);
    return TJS_S_OK;
}
static tjs_error Player_getCanvasCaptureEnabled(tTJSVariant *r, tjs_int, tTJSVariant **,
                                                iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getCanvasCaptureEnabled() : false);
    return TJS_S_OK;
}
static tjs_error Player_getClearEnabled(tTJSVariant *r, tjs_int, tTJSVariant **,
                                        iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getClearEnabled() : false);
    return TJS_S_OK;
}
static tjs_error Player_getHitThreshold(tTJSVariant *r, tjs_int, tTJSVariant **,
                                        iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getHitThreshold() : 0.0);
    return TJS_S_OK;
}
static tjs_error Player_setHitThreshold(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                        iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setHitThreshold(p[0]->AsReal());
    return TJS_S_OK;
}
static tjs_error Player_getPreview(tTJSVariant *r, tjs_int, tTJSVariant **,
                                   iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getPreview() : false);
    return TJS_S_OK;
}
static tjs_error Player_getOutsideFactor(tTJSVariant *r, tjs_int, tTJSVariant **,
                                         iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getOutsideFactor() : 0.0);
    return TJS_S_OK;
}
static tjs_error Player_setOutsideFactor(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                         iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setOutsideFactor(p[0]->AsReal());
    return TJS_S_OK;
}
static tjs_error Player_getResourceManager(tTJSVariant *r, tjs_int, tTJSVariant **,
                                           iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = p ? p->getResourceManager() : tTJSVariant();
    return TJS_S_OK;
}
static tjs_error Player_setResourceManager(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                           iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setResourceManager(*p[0]);
    return TJS_S_OK;
}
static tjs_error Player_getStealthChara(tTJSVariant *r, tjs_int, tTJSVariant **,
                                        iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getStealthChara() : ttstr());
    return TJS_S_OK;
}
static tjs_error Player_setStealthChara(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                        iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setStealthChara(ttstr(*p[0]));
    return TJS_S_OK;
}
static tjs_error Player_getStealthMotion(tTJSVariant *r, tjs_int, tTJSVariant **,
                                         iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getStealthMotion() : ttstr());
    return TJS_S_OK;
}
static tjs_error Player_setStealthMotion(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                         iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setStealthMotion(ttstr(*p[0]));
    return TJS_S_OK;
}
static tjs_error Player_getTags(tTJSVariant *r, tjs_int, tTJSVariant **,
                                iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = p ? p->getTags() : tTJSVariant();
    return TJS_S_OK;
}
static tjs_error Player_setTags(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setTags(*p[0]);
    return TJS_S_OK;
}
static tjs_error Player_getProject(tTJSVariant *r, tjs_int, tTJSVariant **,
                                   iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = p ? p->getProject() : tTJSVariant();
    return TJS_S_OK;
}
static tjs_error Player_setProject(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                   iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setProject(*p[0]);
    return TJS_S_OK;
}
static tjs_error Player_getMeshline(tTJSVariant *r, tjs_int, tTJSVariant **,
                                    iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getMeshline() : ttstr());
    return TJS_S_OK;
}
static tjs_error Player_setMeshline(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                    iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setMeshline(ttstr(*p[0]));
    return TJS_S_OK;
}
static tjs_error Player_getBusy(tTJSVariant *r, tjs_int, tTJSVariant **,
                                iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getBusy() : false);
    return TJS_S_OK;
}
static tjs_error Player_getX(tTJSVariant *r, tjs_int, tTJSVariant **,
                             iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getX() : 0.0);
    return TJS_S_OK;
}
static tjs_error Player_setX(tTJSVariant *, tjs_int count, tTJSVariant **p,
                             iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setX(p[0]->AsReal());
    return TJS_S_OK;
}
static tjs_error Player_getY(tTJSVariant *r, tjs_int, tTJSVariant **,
                             iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getY() : 0.0);
    return TJS_S_OK;
}
static tjs_error Player_setY(tTJSVariant *, tjs_int count, tTJSVariant **p,
                             iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setY(p[0]->AsReal());
    return TJS_S_OK;
}
static tjs_error Player_getCompletionType(tTJSVariant *r, tjs_int, tTJSVariant **,
                                          iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(static_cast<tjs_int>(p ? p->getCompletionType() : 0));
    return TJS_S_OK;
}
static tjs_error Player_setCompletionType(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                          iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setCompletionType(static_cast<int>(p[0]->AsInteger()));
    return TJS_S_OK;
}

// --- Method raw callbacks (simple wrappers) ---

static tjs_error Player_random(tTJSVariant *r, tjs_int, tTJSVariant **,
                               iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->random() : 0.0);
    return TJS_S_OK;
}
static tjs_error Player_initPhysics(tTJSVariant *, tjs_int, tTJSVariant **,
                                    iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(p) p->initPhysics();
    return TJS_S_OK;
}
static tjs_error Player_serialize(tTJSVariant *r, tjs_int, tTJSVariant **,
                                  iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = p ? p->serialize() : tTJSVariant();
    return TJS_S_OK;
}
static tjs_error Player_unserialize(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                    iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->unserialize(*p[0]);
    return TJS_S_OK;
}
static tjs_error Player_setRotate(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                  iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    double rot = p[0]->AsReal();
    double transition = count >= 2 ? p[1]->AsReal() : 0.0;
    double ease = count >= 3 ? p[2]->AsReal() : 0.0;
    pl->setRotate(rot, transition, ease);
    return TJS_S_OK;
}
static tjs_error Player_setMirror(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                  iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setMirror(static_cast<bool>(p[0]->AsInteger()));
    return TJS_S_OK;
}
static tjs_error Player_setHairScale(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                     iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setHairScale(p[0]->AsReal());
    return TJS_S_OK;
}
static tjs_error Player_setPartsScale(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                      iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setPartsScale(p[0]->AsReal());
    return TJS_S_OK;
}
static tjs_error Player_setBustScale(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                     iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setBustScale(p[0]->AsReal());
    return TJS_S_OK;
}
static tjs_error Player_getCameraOffset(tTJSVariant *r, tjs_int, tTJSVariant **,
                                        iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = p ? p->getCameraOffset() : tTJSVariant();
    return TJS_S_OK;
}
static tjs_error Player_setCameraOffset(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                        iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setCameraOffset(*p[0]);
    return TJS_S_OK;
}
static tjs_error Player_modifyRoot(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                   iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->modifyRoot(*p[0]);
    return TJS_S_OK;
}
static tjs_error Player_debugPrint(tTJSVariant *, tjs_int, tTJSVariant **,
                                   iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(p) p->debugPrint();
    return TJS_S_OK;
}
static tjs_error Player_unload(tTJSVariant *, tjs_int count, tTJSVariant **p,
                               iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->unload(ttstr(*p[0]));
    return TJS_S_OK;
}
static tjs_error Player_unloadAll(tTJSVariant *, tjs_int, tTJSVariant **,
                                  iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(p) p->unloadAll();
    return TJS_S_OK;
}
static tjs_error Player_isExistMotion(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                                      iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    if(r) *r = tTJSVariant(pl->isExistMotion(ttstr(*p[0])));
    return TJS_S_OK;
}
static tjs_error Player_findMotion(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                                   iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    if(r) *r = pl->findMotion(ttstr(*p[0]));
    return TJS_S_OK;
}
static tjs_error Player_requireLayerId(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                                       iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    if(r) *r = tTJSVariant(pl->requireLayerId(ttstr(*p[0])));
    return TJS_S_OK;
}
static tjs_error Player_releaseLayerId(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                       iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->releaseLayerId(static_cast<tjs_int>(p[0]->AsInteger()));
    return TJS_S_OK;
}
static tjs_error Player_setClearColor(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                      iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setClearColor(static_cast<tjs_int>(p[0]->AsInteger()));
    return TJS_S_OK;
}
static tjs_error Player_setSize(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 2) return TJS_E_INVALIDPARAM;
    pl->setSize(static_cast<tjs_int>(p[0]->AsInteger()),
                static_cast<tjs_int>(p[1]->AsInteger()));
    return TJS_S_OK;
}
static tjs_error Player_setResizable(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                     iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setResizable(static_cast<bool>(p[0]->AsInteger()));
    return TJS_S_OK;
}
static tjs_error Player_removeAllTextures(tTJSVariant *, tjs_int, tTJSVariant **,
                                          iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(p) p->removeAllTextures();
    return TJS_S_OK;
}
static tjs_error Player_removeAllBg(tTJSVariant *, tjs_int, tTJSVariant **,
                                    iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(p) p->removeAllBg();
    return TJS_S_OK;
}
static tjs_error Player_removeAllCaption(tTJSVariant *, tjs_int, tTJSVariant **,
                                         iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(p) p->removeAllCaption();
    return TJS_S_OK;
}
static tjs_error Player_registerBg(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                   iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->registerBg(*p[0]);
    return TJS_S_OK;
}
static tjs_error Player_registerCaption(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                        iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->registerCaption(*p[0]);
    return TJS_S_OK;
}
static tjs_error Player_unloadUnusedTextures(tTJSVariant *, tjs_int, tTJSVariant **,
                                             iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(p) p->unloadUnusedTextures();
    return TJS_S_OK;
}
static tjs_error Player_alphaOpAdd(tTJSVariant *r, tjs_int, tTJSVariant **,
                                   iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->alphaOpAdd() : 0);
    return TJS_S_OK;
}
static tjs_error Player_findSource(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                                   iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    if(r) *r = pl->findSource(ttstr(*p[0]));
    return TJS_S_OK;
}
static tjs_error Player_loadSource(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                   iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->loadSource(ttstr(*p[0]));
    return TJS_S_OK;
}
static tjs_error Player_clearCache(tTJSVariant *, tjs_int, tTJSVariant **,
                                   iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(p) p->clearCache();
    return TJS_S_OK;
}
static tjs_error Player_copyRect(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                 iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->copyRect(*p[0]);
    return TJS_S_OK;
}
static tjs_error Player_adjustGamma(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                    iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->adjustGamma(*p[0]);
    return TJS_S_OK;
}
static tjs_error Player_frameProgress(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                      iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->frameProgress(p[0]->AsReal());
    return TJS_S_OK;
}
static tjs_error Player_setFlip(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setFlip(static_cast<bool>(p[0]->AsInteger()));
    return TJS_S_OK;
}
static tjs_error Player_setOpacity(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                   iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setOpacity(p[0]->AsReal());
    return TJS_S_OK;
}
static tjs_error Player_setVisible(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                   iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setVisible(static_cast<bool>(p[0]->AsInteger()));
    return TJS_S_OK;
}
static tjs_error Player_setSlant(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                 iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setSlant(p[0]->AsReal());
    return TJS_S_OK;
}
static tjs_error Player_setZoom(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->setZoom(p[0]->AsReal());
    return TJS_S_OK;
}
static tjs_error Player_getLayerNames(tTJSVariant *r, tjs_int, tTJSVariant **,
                                      iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = p ? p->getLayerNames() : tTJSVariant();
    return TJS_S_OK;
}
static tjs_error Player_releaseSyncWait(tTJSVariant *, tjs_int, tTJSVariant **,
                                        iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(p) p->releaseSyncWait();
    return TJS_S_OK;
}
static tjs_error Player_calcViewParam(tTJSVariant *, tjs_int, tTJSVariant **,
                                      iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(p) p->calcViewParam();
    return TJS_S_OK;
}
static tjs_error Player_getLayerMotion(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                                       iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    if(r) *r = pl->getLayerMotion(ttstr(*p[0]));
    return TJS_S_OK;
}
static tjs_error Player_getLayerGetter(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                                       iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    if(r) *r = pl->getLayerGetter(ttstr(*p[0]));
    return TJS_S_OK;
}
static tjs_error Player_getLayerGetterList(tTJSVariant *r, tjs_int, tTJSVariant **,
                                           iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = p ? p->getLayerGetterList() : tTJSVariant();
    return TJS_S_OK;
}
static tjs_error Player_skipToSync(tTJSVariant *, tjs_int, tTJSVariant **,
                                   iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(p) p->skipToSync();
    return TJS_S_OK;
}
static tjs_error Player_setStereovisionCameraPosition(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                                      iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 3) return TJS_E_INVALIDPARAM;
    pl->setStereovisionCameraPosition(p[0]->AsReal(), p[1]->AsReal(), p[2]->AsReal());
    return TJS_S_OK;
}
static tjs_error Player_getVariable(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                                    iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    if(r) *r = tTJSVariant(pl->getVariable(ttstr(*p[0])));
    return TJS_S_OK;
}
static tjs_error Player_countVariables(tTJSVariant *r, tjs_int, tTJSVariant **,
                                       iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->countVariables() : 0);
    return TJS_S_OK;
}
static tjs_error Player_getVariableLabelAt(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                                           iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    if(r) *r = tTJSVariant(pl->getVariableLabelAt(static_cast<tjs_int>(p[0]->AsInteger())));
    return TJS_S_OK;
}
static tjs_error Player_countVariableFrameAt(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                                             iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    if(r) *r = tTJSVariant(pl->countVariableFrameAt(static_cast<tjs_int>(p[0]->AsInteger())));
    return TJS_S_OK;
}
static tjs_error Player_getVariableFrameLabelAt(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                                                iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 2) return TJS_E_INVALIDPARAM;
    if(r) *r = tTJSVariant(pl->getVariableFrameLabelAt(
        static_cast<tjs_int>(p[0]->AsInteger()),
        static_cast<tjs_int>(p[1]->AsInteger())));
    return TJS_S_OK;
}
static tjs_error Player_getVariableFrameValueAt(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                                                iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 2) return TJS_E_INVALIDPARAM;
    if(r) *r = tTJSVariant(pl->getVariableFrameValueAt(
        static_cast<tjs_int>(p[0]->AsInteger()),
        static_cast<tjs_int>(p[1]->AsInteger())));
    return TJS_S_OK;
}
static tjs_error Player_getTimelinePlaying(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                                           iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    if(r) *r = tTJSVariant(pl->getTimelinePlaying(ttstr(*p[0])));
    return TJS_S_OK;
}
static tjs_error Player_getVariableRange(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                                         iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    if(r) *r = pl->getVariableRange(ttstr(*p[0]));
    return TJS_S_OK;
}
static tjs_error Player_getVariableFrameList(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                                             iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    if(r) *r = pl->getVariableFrameList(ttstr(*p[0]));
    return TJS_S_OK;
}
static tjs_error Player_countMainTimelines(tTJSVariant *r, tjs_int, tTJSVariant **,
                                           iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->countMainTimelines() : 0);
    return TJS_S_OK;
}
static tjs_error Player_getMainTimelineLabelAt(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                                               iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    if(r) *r = tTJSVariant(pl->getMainTimelineLabelAt(static_cast<tjs_int>(p[0]->AsInteger())));
    return TJS_S_OK;
}
static tjs_error Player_getMainTimelineLabelList(tTJSVariant *r, tjs_int, tTJSVariant **,
                                                 iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = p ? p->getMainTimelineLabelList() : tTJSVariant();
    return TJS_S_OK;
}
static tjs_error Player_countDiffTimelines(tTJSVariant *r, tjs_int, tTJSVariant **,
                                           iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->countDiffTimelines() : 0);
    return TJS_S_OK;
}
static tjs_error Player_getDiffTimelineLabelAt(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                                               iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    if(r) *r = tTJSVariant(pl->getDiffTimelineLabelAt(static_cast<tjs_int>(p[0]->AsInteger())));
    return TJS_S_OK;
}
static tjs_error Player_getDiffTimelineLabelList(tTJSVariant *r, tjs_int, tTJSVariant **,
                                                 iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = p ? p->getDiffTimelineLabelList() : tTJSVariant();
    return TJS_S_OK;
}
static tjs_error Player_getLoopTimeline(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                                        iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    if(r) *r = tTJSVariant(pl->getLoopTimeline(ttstr(*p[0])));
    return TJS_S_OK;
}
static tjs_error Player_countPlayingTimelines(tTJSVariant *r, tjs_int, tTJSVariant **,
                                              iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->countPlayingTimelines() : 0);
    return TJS_S_OK;
}
static tjs_error Player_getPlayingTimelineLabelAt(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                                                  iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    if(r) *r = tTJSVariant(pl->getPlayingTimelineLabelAt(static_cast<tjs_int>(p[0]->AsInteger())));
    return TJS_S_OK;
}
static tjs_error Player_getPlayingTimelineFlagsAt(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                                                  iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    if(r) *r = tTJSVariant(pl->getPlayingTimelineFlagsAt(static_cast<tjs_int>(p[0]->AsInteger())));
    return TJS_S_OK;
}
static tjs_error Player_getTimelineTotalFrameCount(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                                                   iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    if(r) *r = tTJSVariant(pl->getTimelineTotalFrameCount(ttstr(*p[0])));
    return TJS_S_OK;
}
static tjs_error Player_playTimeline(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                     iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    tjs_int flags = count >= 2 ? static_cast<tjs_int>(p[1]->AsInteger()) : 0;
    pl->playTimeline(ttstr(*p[0]), flags);
    return TJS_S_OK;
}
static tjs_error Player_stopTimeline(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                     iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->stopTimeline(ttstr(*p[0]));
    return TJS_S_OK;
}
static tjs_error Player_setTimelineBlendRatio(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                              iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 2) return TJS_E_INVALIDPARAM;
    pl->setTimelineBlendRatio(ttstr(*p[0]), p[1]->AsReal());
    return TJS_S_OK;
}
static tjs_error Player_getTimelineBlendRatio(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                                              iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    if(r) *r = tTJSVariant(pl->getTimelineBlendRatio(ttstr(*p[0])));
    return TJS_S_OK;
}
static tjs_error Player_fadeInTimeline(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                       iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 2) return TJS_E_INVALIDPARAM;
    tjs_int flags = count >= 3 ? static_cast<tjs_int>(p[2]->AsInteger()) : 0;
    pl->fadeInTimeline(ttstr(*p[0]), p[1]->AsReal(), flags);
    return TJS_S_OK;
}
static tjs_error Player_fadeOutTimeline(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                        iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 2) return TJS_E_INVALIDPARAM;
    tjs_int flags = count >= 3 ? static_cast<tjs_int>(p[2]->AsInteger()) : 0;
    pl->fadeOutTimeline(ttstr(*p[0]), p[1]->AsReal(), flags);
    return TJS_S_OK;
}
static tjs_error Player_getPlayingTimelineInfoList(tTJSVariant *r, tjs_int, tTJSVariant **,
                                                   iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = p ? p->getPlayingTimelineInfoList() : tTJSVariant();
    return TJS_S_OK;
}
static tjs_error Player_isSelectorTarget(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                                         iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    if(r) *r = tTJSVariant(pl->isSelectorTarget(ttstr(*p[0])));
    return TJS_S_OK;
}
static tjs_error Player_deactivateSelectorTarget(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                                 iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->deactivateSelectorTarget(ttstr(*p[0]));
    return TJS_S_OK;
}
static tjs_error Player_getCommandList(tTJSVariant *r, tjs_int, tTJSVariant **,
                                       iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = p ? p->getCommandList() : tTJSVariant();
    return TJS_S_OK;
}
static tjs_error Player_getD3DAvailable(tTJSVariant *r, tjs_int, tTJSVariant **,
                                        iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(p ? p->getD3DAvailable() : false);
    return TJS_S_OK;
}
static tjs_error Player_doAlphaMaskOperation(tTJSVariant *, tjs_int, tTJSVariant **,
                                             iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(p) p->doAlphaMaskOperation();
    return TJS_S_OK;
}
static tjs_error Player_onFindMotion(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                     iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    int flags = count >= 2 ? static_cast<int>(p[1]->AsInteger()) : 0;
    pl->onFindMotion(ttstr(*p[0]), flags);
    return TJS_S_OK;
}
static tjs_error Player_motionList(tTJSVariant *r, tjs_int, tTJSVariant **,
                                   iTJSDispatch2 *objthis) {
    auto *p = GetPlayerInstance(objthis);
    if(r) *r = p ? p->motionList() : tTJSVariant();
    return TJS_S_OK;
}
static tjs_error Player_emoteEdit(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                  iTJSDispatch2 *objthis) {
    auto *pl = GetPlayerInstance(objthis);
    if(!pl || count < 1) return TJS_E_INVALIDPARAM;
    pl->emoteEdit(*p[0]);
    return TJS_S_OK;
}

// --- Player NCB Registration ---

NCB_REGISTER_SUBCLASS_DELAY(Player) {
    NCB_CONSTRUCTOR(());
    // Static properties
    NCB_PROPERTY_RAW_CALLBACK(useD3D, Player::getUseD3DStatic, Player::setUseD3DStatic, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK(enableD3D, Player::getUseD3DStatic, Player::setUseD3DStatic, TJS_STATICMEMBER);
    // Properties
    NCB_PROPERTY_RAW_CALLBACK_RO(playing, Player_getPlaying, 0);
    NCB_PROPERTY_RAW_CALLBACK_RO(allplaying, Player_getAllplaying, 0);
    NCB_PROPERTY_RAW_CALLBACK(motion, Player::getMotionCompat, Player::setMotionCompat, 0);
    NCB_PROPERTY_RAW_CALLBACK(motionKey, Player_getMotionKey, Player_setMotionKey, 0);
    NCB_PROPERTY_RAW_CALLBACK(chara, Player_getChara, Player_setChara, 0);
    NCB_PROPERTY_RAW_CALLBACK(outline, Player_getOutline, Player_setOutline, 0);
    NCB_PROPERTY_RAW_CALLBACK(priorDraw, Player_getPriorDraw, Player_setPriorDraw, 0);
    NCB_PROPERTY_RAW_CALLBACK(frameLastTime, Player_getFrameLastTime, Player_setFrameLastTime, 0);
    NCB_PROPERTY_RAW_CALLBACK(frameLoopTime, Player_getFrameLoopTime, Player_setFrameLoopTime, 0);
    NCB_PROPERTY_RAW_CALLBACK(loopTime, Player_getLoopTime, Player_setLoopTime, 0);
    NCB_PROPERTY_RAW_CALLBACK_RO(processedMeshVerticesNum, Player_getProcessedMeshVerticesNum, 0);
    NCB_PROPERTY_RAW_CALLBACK(queuing, Player_getQueuing, Player_setQueuing, 0);
    NCB_PROPERTY_RAW_CALLBACK(directEdit, Player_getDirectEdit, Player_setDirectEdit, 0);
    NCB_PROPERTY_RAW_CALLBACK(selectorEnabled, Player_getSelectorEnabled, Player_setSelectorEnabled, 0);
    NCB_PROPERTY_RAW_CALLBACK_RO(variableKeys, Player_getVariableKeys, 0);
    NCB_PROPERTY_RAW_CALLBACK_RO(syncWaiting, Player_getSyncWaiting, 0);
    NCB_PROPERTY_RAW_CALLBACK_RO(syncActive, Player_getSyncActive, 0);
    NCB_PROPERTY_RAW_CALLBACK_RO(hasCamera, Player_getHasCamera, 0);
    NCB_PROPERTY_RAW_CALLBACK_RO(cameraActive, Player_getCameraActive, 0);
    NCB_PROPERTY_RAW_CALLBACK_RO(stereovisionActive, Player_getStereovisionActive, 0);
    NCB_PROPERTY_RAW_CALLBACK(tickCount, Player_getTickCount, Player_setTickCount, 0);
    NCB_PROPERTY_RAW_CALLBACK(speed, Player_getSpeed, Player_setSpeed, 0);
    NCB_PROPERTY_RAW_CALLBACK(frameTickCount, Player_getFrameTickCount, Player_setFrameTickCount, 0);
    NCB_PROPERTY_RAW_CALLBACK(colorWeight, Player_getColorWeight, Player_setColorWeight, 0);
    NCB_PROPERTY_RAW_CALLBACK(maskMode, Player_getMaskMode, Player_setMaskMode, 0);
    NCB_PROPERTY_RAW_CALLBACK(independentLayerInherit, Player_getIndependentLayerInherit, Player_setIndependentLayerInherit, 0);
    NCB_PROPERTY_RAW_CALLBACK(zFactor, Player_getZFactor, Player_setZFactor, 0);
    NCB_PROPERTY_RAW_CALLBACK(cameraTarget, Player_getCameraTarget, Player_setCameraTarget, 0);
    NCB_PROPERTY_RAW_CALLBACK(cameraPosition, Player_getCameraPosition, Player_setCameraPosition, 0);
    NCB_PROPERTY_RAW_CALLBACK(cameraFOV, Player_getCameraFOV, Player_setCameraFOV, 0);
    NCB_PROPERTY_RAW_CALLBACK_RO(cameraAlive, Player_getCameraAlive, 0);
    NCB_PROPERTY_RAW_CALLBACK_RO(canvasCaptureEnabled, Player_getCanvasCaptureEnabled, 0);
    NCB_PROPERTY_RAW_CALLBACK_RO(clearEnabled, Player_getClearEnabled, 0);
    NCB_PROPERTY_RAW_CALLBACK(hitThreshold, Player_getHitThreshold, Player_setHitThreshold, 0);
    NCB_PROPERTY_RAW_CALLBACK_RO(preview, Player_getPreview, 0);
    NCB_PROPERTY_RAW_CALLBACK(outsideFactor, Player_getOutsideFactor, Player_setOutsideFactor, 0);
    NCB_PROPERTY_RAW_CALLBACK(resourceManager, Player_getResourceManager, Player_setResourceManager, 0);
    NCB_PROPERTY_RAW_CALLBACK(stealthChara, Player_getStealthChara, Player_setStealthChara, 0);
    NCB_PROPERTY_RAW_CALLBACK(stealthMotion, Player_getStealthMotion, Player_setStealthMotion, 0);
    NCB_PROPERTY_RAW_CALLBACK(tags, Player_getTags, Player_setTags, 0);
    NCB_PROPERTY_RAW_CALLBACK(project, Player_getProject, Player_setProject, 0);
    NCB_PROPERTY_RAW_CALLBACK(meshline, Player_getMeshline, Player_setMeshline, 0);
    NCB_PROPERTY_RAW_CALLBACK_RO(busy, Player_getBusy, 0);
    NCB_PROPERTY_RAW_CALLBACK(x, Player_getX, Player_setX, 0);
    NCB_PROPERTY_RAW_CALLBACK(y, Player_getY, Player_setY, 0);
    NCB_PROPERTY_RAW_CALLBACK(left, Player_getX, Player_setX, 0);
    NCB_PROPERTY_RAW_CALLBACK(top, Player_getY, Player_setY, 0);
    NCB_PROPERTY_RAW_CALLBACK(completionType, Player_getCompletionType, Player_setCompletionType, 0);
    NCB_PROPERTY_RAW_CALLBACK_RO(metadata, Player_getVariableKeys, 0);
    // Methods (using static compat methods for objthis access)
    NCB_METHOD_RAW_CALLBACK(draw, Player::drawCompat, 0);
    NCB_METHOD_RAW_CALLBACK(play, Player::playCompat, 0);
    NCB_METHOD_RAW_CALLBACK(progress, Player::progressCompatMethod, 0);
    NCB_METHOD_RAW_CALLBACK(stop, Player::stopCompat, 0);
    NCB_METHOD_RAW_CALLBACK(isPlaying, Player::isPlayingCompat, 0);
    NCB_METHOD_RAW_CALLBACK(setVariable, Player::setVariableCompatMethod, 0);
    NCB_METHOD_RAW_CALLBACK(setDrawAffineTranslateMatrix, Player::setDrawAffineTranslateMatrixCompat, 0);
    NCB_METHOD_RAW_CALLBACK(captureCanvas, Player::captureCanvasCompat, 0);
    // Methods (simple wrappers)
    NCB_METHOD_RAW_CALLBACK(random, Player_random, 0);
    NCB_METHOD_RAW_CALLBACK(initPhysics, Player_initPhysics, 0);
    NCB_METHOD_RAW_CALLBACK(serialize, Player_serialize, 0);
    NCB_METHOD_RAW_CALLBACK(unserialize, Player_unserialize, 0);
    NCB_METHOD_RAW_CALLBACK(setRotate, Player_setRotate, 0);
    NCB_METHOD_RAW_CALLBACK(setMirror, Player_setMirror, 0);
    NCB_METHOD_RAW_CALLBACK(setHairScale, Player_setHairScale, 0);
    NCB_METHOD_RAW_CALLBACK(setPartsScale, Player_setPartsScale, 0);
    NCB_METHOD_RAW_CALLBACK(setBustScale, Player_setBustScale, 0);
    NCB_METHOD_RAW_CALLBACK(getCameraOffset, Player_getCameraOffset, 0);
    NCB_METHOD_RAW_CALLBACK(setCameraOffset, Player_setCameraOffset, 0);
    NCB_METHOD_RAW_CALLBACK(modifyRoot, Player_modifyRoot, 0);
    NCB_METHOD_RAW_CALLBACK(debugPrint, Player_debugPrint, 0);
    NCB_METHOD_RAW_CALLBACK(unload, Player_unload, 0);
    NCB_METHOD_RAW_CALLBACK(unloadAll, Player_unloadAll, 0);
    NCB_METHOD_RAW_CALLBACK(isExistMotion, Player_isExistMotion, 0);
    NCB_METHOD_RAW_CALLBACK(findMotion, Player_findMotion, 0);
    NCB_METHOD_RAW_CALLBACK(requireLayerId, Player_requireLayerId, 0);
    NCB_METHOD_RAW_CALLBACK(releaseLayerId, Player_releaseLayerId, 0);
    NCB_METHOD_RAW_CALLBACK(setClearColor, Player_setClearColor, 0);
    NCB_METHOD_RAW_CALLBACK(setSize, Player_setSize, 0);
    NCB_METHOD_RAW_CALLBACK(setResizable, Player_setResizable, 0);
    NCB_METHOD_RAW_CALLBACK(removeAllTextures, Player_removeAllTextures, 0);
    NCB_METHOD_RAW_CALLBACK(removeAllBg, Player_removeAllBg, 0);
    NCB_METHOD_RAW_CALLBACK(removeAllCaption, Player_removeAllCaption, 0);
    NCB_METHOD_RAW_CALLBACK(registerBg, Player_registerBg, 0);
    NCB_METHOD_RAW_CALLBACK(registerCaption, Player_registerCaption, 0);
    NCB_METHOD_RAW_CALLBACK(unloadUnusedTextures, Player_unloadUnusedTextures, 0);
    NCB_METHOD_RAW_CALLBACK(alphaOpAdd, Player_alphaOpAdd, 0);
    NCB_METHOD_RAW_CALLBACK(findSource, Player_findSource, 0);
    NCB_METHOD_RAW_CALLBACK(loadSource, Player_loadSource, 0);
    NCB_METHOD_RAW_CALLBACK(clearCache, Player_clearCache, 0);
    NCB_METHOD_RAW_CALLBACK(copyRect, Player_copyRect, 0);
    NCB_METHOD_RAW_CALLBACK(adjustGamma, Player_adjustGamma, 0);
    NCB_METHOD_RAW_CALLBACK(frameProgress, Player_frameProgress, 0);
    NCB_METHOD_RAW_CALLBACK(setFlip, Player_setFlip, 0);
    NCB_METHOD_RAW_CALLBACK(setOpacity, Player_setOpacity, 0);
    NCB_METHOD_RAW_CALLBACK(setVisible, Player_setVisible, 0);
    NCB_METHOD_RAW_CALLBACK(setSlant, Player_setSlant, 0);
    NCB_METHOD_RAW_CALLBACK(setZoom, Player_setZoom, 0);
    NCB_METHOD_RAW_CALLBACK(getLayerNames, Player_getLayerNames, 0);
    NCB_METHOD_RAW_CALLBACK(releaseSyncWait, Player_releaseSyncWait, 0);
    NCB_METHOD_RAW_CALLBACK(calcViewParam, Player_calcViewParam, 0);
    NCB_METHOD_RAW_CALLBACK(getLayerMotion, Player_getLayerMotion, 0);
    NCB_METHOD_RAW_CALLBACK(getLayerGetter, Player_getLayerGetter, 0);
    NCB_METHOD_RAW_CALLBACK(getLayerGetterList, Player_getLayerGetterList, 0);
    NCB_METHOD_RAW_CALLBACK(skipToSync, Player_skipToSync, 0);
    NCB_METHOD_RAW_CALLBACK(setStereovisionCameraPosition, Player_setStereovisionCameraPosition, 0);
    NCB_METHOD_RAW_CALLBACK(getVariable, Player_getVariable, 0);
    NCB_METHOD_RAW_CALLBACK(countVariables, Player_countVariables, 0);
    NCB_METHOD_RAW_CALLBACK(getVariableLabelAt, Player_getVariableLabelAt, 0);
    NCB_METHOD_RAW_CALLBACK(countVariableFrameAt, Player_countVariableFrameAt, 0);
    NCB_METHOD_RAW_CALLBACK(getVariableFrameLabelAt, Player_getVariableFrameLabelAt, 0);
    NCB_METHOD_RAW_CALLBACK(getVariableFrameValueAt, Player_getVariableFrameValueAt, 0);
    NCB_METHOD_RAW_CALLBACK(getTimelinePlaying, Player_getTimelinePlaying, 0);
    NCB_METHOD_RAW_CALLBACK(getVariableRange, Player_getVariableRange, 0);
    NCB_METHOD_RAW_CALLBACK(getVariableFrameList, Player_getVariableFrameList, 0);
    NCB_METHOD_RAW_CALLBACK(countMainTimelines, Player_countMainTimelines, 0);
    NCB_METHOD_RAW_CALLBACK(getMainTimelineLabelAt, Player_getMainTimelineLabelAt, 0);
    NCB_METHOD_RAW_CALLBACK(getMainTimelineLabelList, Player_getMainTimelineLabelList, 0);
    NCB_METHOD_RAW_CALLBACK(countDiffTimelines, Player_countDiffTimelines, 0);
    NCB_METHOD_RAW_CALLBACK(getDiffTimelineLabelAt, Player_getDiffTimelineLabelAt, 0);
    NCB_METHOD_RAW_CALLBACK(getDiffTimelineLabelList, Player_getDiffTimelineLabelList, 0);
    NCB_METHOD_RAW_CALLBACK(getLoopTimeline, Player_getLoopTimeline, 0);
    NCB_METHOD_RAW_CALLBACK(countPlayingTimelines, Player_countPlayingTimelines, 0);
    NCB_METHOD_RAW_CALLBACK(getPlayingTimelineLabelAt, Player_getPlayingTimelineLabelAt, 0);
    NCB_METHOD_RAW_CALLBACK(getPlayingTimelineFlagsAt, Player_getPlayingTimelineFlagsAt, 0);
    NCB_METHOD_RAW_CALLBACK(getTimelineTotalFrameCount, Player_getTimelineTotalFrameCount, 0);
    NCB_METHOD_RAW_CALLBACK(playTimeline, Player_playTimeline, 0);
    NCB_METHOD_RAW_CALLBACK(stopTimeline, Player_stopTimeline, 0);
    NCB_METHOD_RAW_CALLBACK(setTimelineBlendRatio, Player_setTimelineBlendRatio, 0);
    NCB_METHOD_RAW_CALLBACK(getTimelineBlendRatio, Player_getTimelineBlendRatio, 0);
    NCB_METHOD_RAW_CALLBACK(fadeInTimeline, Player_fadeInTimeline, 0);
    NCB_METHOD_RAW_CALLBACK(fadeOutTimeline, Player_fadeOutTimeline, 0);
    NCB_METHOD_RAW_CALLBACK(getPlayingTimelineInfoList, Player_getPlayingTimelineInfoList, 0);
    NCB_METHOD_RAW_CALLBACK(isSelectorTarget, Player_isSelectorTarget, 0);
    NCB_METHOD_RAW_CALLBACK(deactivateSelectorTarget, Player_deactivateSelectorTarget, 0);
    NCB_METHOD_RAW_CALLBACK(getCommandList, Player_getCommandList, 0);
    NCB_METHOD_RAW_CALLBACK(getD3DAvailable, Player_getD3DAvailable, 0);
    NCB_METHOD_RAW_CALLBACK(doAlphaMaskOperation, Player_doAlphaMaskOperation, 0);
    NCB_METHOD_RAW_CALLBACK(onFindMotion, Player_onFindMotion, 0);
    NCB_METHOD_RAW_CALLBACK(motionList, Player_motionList, 0);
    NCB_METHOD_RAW_CALLBACK(emoteEdit, Player_emoteEdit, 0);
    NCB_METHOD_RAW_CALLBACK(missing, Universal_missing_method, 0);
}

// ============================================================================
// EmotePlayer — raw callback wrappers
// ============================================================================

static motion::EmotePlayer *GetEmotePlayerInstance(iTJSDispatch2 *objthis) {
    return ncbInstanceAdaptor<motion::EmotePlayer>::GetNativeInstance(objthis);
}

NCB_REGISTER_SUBCLASS_DELAY(EmotePlayer) {
    NCB_CONSTRUCTOR((ResourceManager));
    NCB_PROPERTY(useD3D, getUseD3D, setUseD3D);
    NCB_PROPERTY(smoothing, getSmoothing, setSmoothing);
    NCB_PROPERTY(meshDivisionRatio, getMeshDivisionRatio, setMeshDivisionRatio);
    NCB_PROPERTY(queing, getQueuing, setQueuing);   // original typo preserved
    NCB_PROPERTY(hairScale, getHairScale, setHairScale);
    NCB_PROPERTY(partsScale, getPartsScale, setPartsScale);
    NCB_PROPERTY(bustScale, getBustScale, setBustScale);
    NCB_PROPERTY(bodyScale, getBodyScale, setBodyScale);
    NCB_PROPERTY(visible, getVisible, setVisible);
    NCB_PROPERTY_RO(animating, getAnimating);
    NCB_PROPERTY(progress, getProgress, setProgress);
    NCB_PROPERTY(modified, getModified, setModified);
    NCB_PROPERTY(drawvisible, getDrawVisible, setDrawVisible);
    NCB_PROPERTY(drawOpacity, getDrawOpacity, setDrawOpacity);
    NCB_PROPERTY(opengl, getOpengl, setOpengl);
    NCB_PROPERTY_RO(module, getModule);
    NCB_PROPERTY_RO(playCallback, getPlayCallback);
    // Methods via native NCB
    NCB_METHOD(create);
    NCB_METHOD(load);
    NCB_METHOD(clone);
    NCB_METHOD(show);
    NCB_METHOD(hide);
    NCB_METHOD(assignState);
    NCB_METHOD(initPhysics);
    // Methods with optional args via compat
    NCB_METHOD_RAW_CALLBACK(setRot, EmotePlayer::setRotCompat, 0);
    NCB_METHOD_RAW_CALLBACK(setCoord, EmotePlayer::setCoordCompat, 0);
    NCB_METHOD_RAW_CALLBACK(setScale, EmotePlayer::setScaleCompat, 0);
    NCB_METHOD_RAW_CALLBACK(setColor, EmotePlayer::setColorCompat, 0);
    NCB_METHOD_RAW_CALLBACK(setVariable, EmotePlayer::setVariableCompat, 0);
    NCB_METHOD_RAW_CALLBACK(startWind, EmotePlayer::startWindCompat, 0);
    NCB_METHOD_RAW_CALLBACK(stopWind, EmotePlayer::stopWindCompat, 0);
    NCB_METHOD_RAW_CALLBACK(setOuterForce, EmotePlayer::setOuterForceCompat, 0);
    NCB_METHOD_RAW_CALLBACK(contains, EmotePlayer::containsCompat, 0);
    // Simple delegation methods
    NCB_METHOD(getRot);
    NCB_METHOD(getScale);
    NCB_METHOD(setMirror);
    NCB_METHOD(getColor);
    NCB_METHOD(countVariables);
    NCB_METHOD(getVariableLabelAt);
    NCB_METHOD(countVariableFrameAt);
    NCB_METHOD(getVariableFrameLabelAt);
    NCB_METHOD(getVariableFrameValueAt);
    NCB_METHOD(getVariable);
    NCB_METHOD(countMainTimelines);
    NCB_METHOD(getMainTimelineLabelAt);
    NCB_METHOD(countDiffTimelines);
    NCB_METHOD(getDiffTimelineLabelAt);
    NCB_METHOD(countPlayingTimelines);
    NCB_METHOD(getPlayingTimelineLabelAt);
    NCB_METHOD(getPlayingTimelineFlagsAt);
    NCB_METHOD(isLoopTimeline);
    NCB_METHOD(getTimelineTotalFrameCount);
    NCB_METHOD(playTimeline);
    NCB_METHOD(isTimelinePlaying);
    NCB_METHOD(stopTimeline);
    NCB_METHOD(setTimelineBlendRatio);
    NCB_METHOD(getTimelineBlendRatio);
    NCB_METHOD(fadeInTimeline);
    NCB_METHOD(fadeOutTimeline);
    NCB_METHOD(setTimeline);
    NCB_METHOD(skip);
    NCB_METHOD(addPlayCallback);
    NCB_METHOD(pass);
    NCB_METHOD(progress);
    NCB_METHOD(getOuterForce);
    NCB_METHOD_RAW_CALLBACK(missing, Universal_missing_method, 0);
}

// ============================================================================
// ResourceManager
// ============================================================================

static tjs_error ResourceManager_unload(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                        iTJSDispatch2 *objthis) {
    auto *manager = ncbInstanceAdaptor<motion::ResourceManager>::GetNativeInstance(objthis);
    if(!manager || count < 1) return TJS_E_INVALIDPARAM;
    manager->unload(ttstr(*p[0]));
    return TJS_S_OK;
}
static tjs_error ResourceManager_clearCache(tTJSVariant *, tjs_int, tTJSVariant **,
                                            iTJSDispatch2 *objthis) {
    auto *manager = ncbInstanceAdaptor<motion::ResourceManager>::GetNativeInstance(objthis);
    if(!manager) return TJS_E_INVALIDPARAM;
    manager->clearCache();
    return TJS_S_OK;
}

NCB_REGISTER_SUBCLASS(ResourceManager) {
    NCB_CONSTRUCTOR((iTJSDispatch2 *, tjs_int));
    NCB_METHOD(load);
    NCB_METHOD_RAW_CALLBACK(unload, ResourceManager_unload, 0);
    NCB_METHOD_RAW_CALLBACK(clearCache, ResourceManager_clearCache, 0);
    NCB_METHOD_RAW_CALLBACK(setEmotePSBDecryptSeed,
                            &ResourceManager::setEmotePSBDecryptSeed,
                            TJS_STATICMEMBER);
    NCB_METHOD_RAW_CALLBACK(setEmotePSBDecryptFunc,
                            &ResourceManager::setEmotePSBDecryptFunc,
                            TJS_STATICMEMBER);
    NCB_METHOD_RAW_CALLBACK(missing, Universal_missing_method, 0);
}

// ============================================================================
// Motion — top-level namespace class with constants
// ============================================================================

class Motion {
public:
    // PlayFlag constants
    static tjs_error getPlayFlagForce(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
        if(r) *r = tTJSVariant(static_cast<tjs_int>(1)); return TJS_S_OK;
    }
    static tjs_error getPlayFlagChain(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
        if(r) *r = tTJSVariant(static_cast<tjs_int>(2)); return TJS_S_OK;
    }
    static tjs_error getPlayFlagAsCan(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
        if(r) *r = tTJSVariant(static_cast<tjs_int>(4)); return TJS_S_OK;
    }
    static tjs_error getPlayFlagJoin(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
        if(r) *r = tTJSVariant(static_cast<tjs_int>(8)); return TJS_S_OK;
    }
    static tjs_error getPlayFlagStealth(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
        if(r) *r = tTJSVariant(static_cast<tjs_int>(16)); return TJS_S_OK;
    }
    // ShapeType constants
    static tjs_error getShapeTypePoint(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
        if(r) *r = tTJSVariant(static_cast<tjs_int>(0)); return TJS_S_OK;
    }
    static tjs_error getShapeTypeCircle(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
        if(r) *r = tTJSVariant(static_cast<tjs_int>(1)); return TJS_S_OK;
    }
    static tjs_error getShapeTypeRect(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
        if(r) *r = tTJSVariant(static_cast<tjs_int>(2)); return TJS_S_OK;
    }
    static tjs_error getShapeTypeQuad(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
        if(r) *r = tTJSVariant(static_cast<tjs_int>(3)); return TJS_S_OK;
    }
    // LayerType constants
    static tjs_error getLayerTypeObj(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
        if(r) *r = tTJSVariant(static_cast<tjs_int>(0)); return TJS_S_OK;
    }
    static tjs_error getLayerTypeShape(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
        if(r) *r = tTJSVariant(static_cast<tjs_int>(1)); return TJS_S_OK;
    }
    static tjs_error getLayerTypeLayout(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
        if(r) *r = tTJSVariant(static_cast<tjs_int>(2)); return TJS_S_OK;
    }
    static tjs_error getLayerTypeMotion(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
        if(r) *r = tTJSVariant(static_cast<tjs_int>(3)); return TJS_S_OK;
    }
    static tjs_error getLayerTypeParticle(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
        if(r) *r = tTJSVariant(static_cast<tjs_int>(4)); return TJS_S_OK;
    }
    static tjs_error getLayerTypeCamera(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
        if(r) *r = tTJSVariant(static_cast<tjs_int>(5)); return TJS_S_OK;
    }
    // TransformOrder constants
    static tjs_error getTransformOrderFlip(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
        if(r) *r = tTJSVariant(static_cast<tjs_int>(0)); return TJS_S_OK;
    }
    static tjs_error getTransformOrderAngle(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
        if(r) *r = tTJSVariant(static_cast<tjs_int>(1)); return TJS_S_OK;
    }
    static tjs_error getTransformOrderZoom(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
        if(r) *r = tTJSVariant(static_cast<tjs_int>(2)); return TJS_S_OK;
    }
    static tjs_error getTransformOrderSlant(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
        if(r) *r = tTJSVariant(static_cast<tjs_int>(3)); return TJS_S_OK;
    }
    // CoordinateType constants
    static tjs_error getCoordinateRecutangularXY(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
        if(r) *r = tTJSVariant(static_cast<tjs_int>(0)); return TJS_S_OK;
    }
    static tjs_error getCoordinateRecutangularXZ(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
        if(r) *r = tTJSVariant(static_cast<tjs_int>(1)); return TJS_S_OK;
    }
    // enableD3D stub
    static tjs_error setEnableD3D(tTJSVariant *, tjs_int count, tTJSVariant **p, iTJSDispatch2 *) {
        return TJS_S_OK;
    }
    static tjs_error getEnableD3D(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
        iTJSDispatch2 *obj = TJSCreateDictionaryObject();
        if(obj) { *r = tTJSVariant(obj); obj->Release(); }
        else { *r = tTJSVariant(); }
        return TJS_S_OK;
    }
};

// ============================================================================
// GenericMockObject — catch-all for unimplemented TJS properties
// ============================================================================

class GenericMockObject : public tTJSDispatch {
    tjs_uint RefCount;
public:
    GenericMockObject() : RefCount(1) {}
    ~GenericMockObject() override {}

    tjs_uint AddRef() override { return ++RefCount; }
    tjs_uint Release() override {
        if(--RefCount == 0) { delete this; return 0; }
        return RefCount;
    }
    tjs_error FuncCall(tjs_uint32 flag, const tjs_char *membername,
                       tjs_uint32 *hint, tTJSVariant *result,
                       tjs_int numparams, tTJSVariant **param,
                       iTJSDispatch2 *objthis) override {
        if(result) { this->AddRef(); *result = tTJSVariant(this, this); }
        return TJS_S_OK;
    }
    tjs_error PropGet(tjs_uint32 flag, const tjs_char *membername,
                      tjs_uint32 *hint, tTJSVariant *result,
                      iTJSDispatch2 *objthis) override {
        if(result) {
            if(membername) {
                if(!TJS_strcmp(membername, TJS_W("count")) || !TJS_strcmp(membername, TJS_W("length"))) {
                    *result = tTJSVariant((tjs_int)0);
                    return TJS_S_OK;
                }
            }
            this->AddRef();
            *result = tTJSVariant(this, this);
        }
        return TJS_S_OK;
    }
    tjs_error PropSet(tjs_uint32 flag, const tjs_char *membername,
                      tjs_uint32 *hint, const tTJSVariant *param,
                      iTJSDispatch2 *objthis) override { return TJS_S_OK; }
    tjs_error CreateNew(tjs_uint32 flag, const tjs_char *membername,
                        tjs_uint32 *hint, iTJSDispatch2 **result,
                        tjs_int numparams, tTJSVariant **param,
                        iTJSDispatch2 *objthis) override {
        if(result) { this->AddRef(); *result = this; }
        return TJS_S_OK;
    }
    tjs_error GetCount(tjs_int *result, const tjs_char *membername,
                       tjs_uint32 *hint, iTJSDispatch2 *objthis) override {
        if(result) *result = 0; return TJS_S_OK;
    }
    tjs_error EnumMembers(tjs_uint32 flag, tTJSVariantClosure *callback,
                          iTJSDispatch2 *objthis) override { return TJS_S_OK; }
    tjs_error DeleteMember(tjs_uint32 flag, const tjs_char *membername,
                           tjs_uint32 *hint, iTJSDispatch2 *objthis) override { return TJS_S_OK; }
    tjs_error Invalidate(tjs_uint32 flag, const tjs_char *membername,
                         tjs_uint32 *hint, iTJSDispatch2 *objthis) override { return TJS_S_OK; }
    tjs_error IsValid(tjs_uint32 flag, const tjs_char *membername,
                      tjs_uint32 *hint, iTJSDispatch2 *objthis) override { return TJS_S_TRUE; }
    tjs_error IsInstanceOf(tjs_uint32 flag, const tjs_char *membername,
                           tjs_uint32 *hint, const tjs_char *classname,
                           iTJSDispatch2 *objthis) override { return TJS_S_TRUE; }
    tjs_error Operation(tjs_uint32 flag, const tjs_char *membername,
                        tjs_uint32 *hint, tTJSVariant *result,
                        const tTJSVariant *param,
                        iTJSDispatch2 *objthis) override {
        if(result) *result = tTJSVariant(); return TJS_S_OK;
    }
};

static tjs_error Universal_missing_method(tTJSVariant *result, tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis) {
    if(!TJS::TVPIsMockEnabled()) {
        if(result) *result = tTJSVariant(static_cast<tjs_int>(0));
        return TJS_S_OK;
    }
    if(numparams >= 3) {
        bool is_set = (tjs_int)*param[0];
        if(!is_set) {
            iTJSDispatch2* prop = param[2]->AsObjectNoAddRef();
            static iTJSDispatch2* dummy = new GenericMockObject();
            tTJSVariant dummy_var(dummy, dummy);
            prop->PropSet(0, nullptr, nullptr, &dummy_var, prop);
        }
    }
    if(result) *result = tTJSVariant(static_cast<tjs_int>(1));
    return TJS_S_OK;
}

static tjs_error Motion_getD3DAdaptor(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
    static iTJSDispatch2* dummy = new GenericMockObject();
    if(r) { dummy->AddRef(); *r = tTJSVariant(dummy, dummy); }
    return TJS_S_OK;
}

NCB_REGISTER_CLASS(Motion) {
    NCB_PROPERTY_RAW_CALLBACK(enableD3D, Motion::getEnableD3D, Motion::setEnableD3D, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(PlayFlagForce, Motion::getPlayFlagForce, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(PlayFlagChain, Motion::getPlayFlagChain, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(PlayFlagAsCan, Motion::getPlayFlagAsCan, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(PlayFlagJoin, Motion::getPlayFlagJoin, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(PlayFlagStealth, Motion::getPlayFlagStealth, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(ShapeTypePoint, Motion::getShapeTypePoint, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(ShapeTypeCircle, Motion::getShapeTypeCircle, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(ShapeTypeRect, Motion::getShapeTypeRect, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(ShapeTypeQuad, Motion::getShapeTypeQuad, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(LayerTypeObj, Motion::getLayerTypeObj, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(LayerTypeShape, Motion::getLayerTypeShape, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(LayerTypeLayout, Motion::getLayerTypeLayout, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(LayerTypeMotion, Motion::getLayerTypeMotion, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(LayerTypeParticle, Motion::getLayerTypeParticle, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(LayerTypeCamera, Motion::getLayerTypeCamera, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(TransformOrderFlip, Motion::getTransformOrderFlip, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(TransformOrderAngle, Motion::getTransformOrderAngle, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(TransformOrderZoom, Motion::getTransformOrderZoom, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(TransformOrderSlant, Motion::getTransformOrderSlant, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(CoordinateRecutangularXY, Motion::getCoordinateRecutangularXY, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(CoordinateRecutangularXZ, Motion::getCoordinateRecutangularXZ, TJS_STATICMEMBER);
    NCB_SUBCLASS(ResourceManager, ResourceManager);
    NCB_SUBCLASS(Player, Player);
    NCB_SUBCLASS(EmotePlayer, EmotePlayer);
    NCB_SUBCLASS(SeparateLayerAdaptor, SeparateLayerAdaptor);
    NCB_PROPERTY_RAW_CALLBACK_RO(D3DAdaptor, Motion_getD3DAdaptor, TJS_STATICMEMBER);
}

// ============================================================================
// Pre-registration callbacks
// ============================================================================

class StaticGlobalMockFunc : public tTJSDispatch {
    ttstr Name;
public:
    StaticGlobalMockFunc(const tjs_char* name) : Name(name) {}
    ~StaticGlobalMockFunc() override {}
    tjs_error FuncCall(tjs_uint32 flag, const tjs_char *membername,
                       tjs_uint32 *hint, tTJSVariant *result,
                       tjs_int numparams, tTJSVariant **param,
                       iTJSDispatch2 *objthis) override {
        if(result) {
            static iTJSDispatch2* dummy = new GenericMockObject();
            dummy->AddRef();
            *result = tTJSVariant(dummy, dummy);
        }
        return TJS_S_OK;
    }
};

static void PreRegistCallback() {
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(global) {
        iTJSDispatch2 *func = new StaticGlobalMockFunc(TJS_W("SetSystemConfigDefaults"));
        tTJSVariant val(func, func);
        global->PropSet(TJS_MEMBERENSURE, TJS_W("SetSystemConfigDefaults"), nullptr, &val, global);

        tTJSVariant sysVar;
        if(TJS_SUCCEEDED(global->PropGet(0, TJS_W("System"), nullptr, &sysVar, global)) && sysVar.Type() == tvtObject) {
            iTJSDispatch2 *sysObj = sysVar.AsObjectNoAddRef();
            if(sysObj) {
                sysObj->PropSet(TJS_MEMBERENSURE, TJS_W("SetSystemConfigDefaults"), nullptr, &val, sysObj);
            }
        }
        func->Release();
        global->Release();
    }
}

static void PostUnregistCallback() {}

NCB_PRE_REGIST_CALLBACK(PreRegistCallback);
NCB_POST_UNREGIST_CALLBACK(PostUnregistCallback);
