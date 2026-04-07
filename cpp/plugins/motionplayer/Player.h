//
// Created by LiDon on 2025/9/15.
//
#pragma once

#include <vector>
#include <set>
#include <unordered_map>
#include <algorithm>
#include <spdlog/spdlog.h>
#include "ResourceManager.h"
#include "tjs.h"
#include "tjsArray.h"
#include "tjsDictionary.h"
#include "../core/base/StorageIntf.h"
#include "../core/base/ScriptMgnIntf.h"
#include "../psbfile/PSBMedia.h"
#include "SeparateLayerAdaptor.h"
#include "ncbind.hpp"

namespace motion {

    class ShapeContainsFunc : public tTJSDispatch {
        int _l, _t, _w, _h;
    public:
        ShapeContainsFunc(int l, int t, int w, int h) : _l(l), _t(t), _w(w), _h(h) {}
        tjs_error FuncCall(
            tjs_uint32 flag, const tjs_char *membername, tjs_uint32 *hint,
            tTJSVariant *result, tjs_int numparams, tTJSVariant **param,
            iTJSDispatch2 *objthis) override {
            if(membername) return TJS_E_MEMBERNOTFOUND;
            if(numparams < 2) return TJS_E_BADPARAMCOUNT;
            int x = static_cast<int>(param[0]->AsInteger());
            int y = static_cast<int>(param[1]->AsInteger());
            if(result) {
                *result = (x >= _l && x < _l + _w && y >= _t && y < _t + _h);
            }
            return TJS_S_OK;
        }
    };

    class Player {
        static std::shared_ptr<spdlog::logger> _logger() {
            return spdlog::get("plugin");
        }

    public:
        Player() = default;
        ~Player() { cleanupTempLayer(); }

        Player(const Player &) = delete;
        Player &operator=(const Player &) = delete;

        static bool getUseD3D() { return _useD3D; }
        static void setUseD3D(bool v) { _useD3D = v; }
        static bool getEnableD3D() { return _enableD3D; }
        static void setEnableD3D(bool v) { _enableD3D = v; }

        bool getPlaying() const { return _playing; }
        bool getAllplaying() const { return _allplaying; }
        ttstr getMotion() const { return _motion; }
        void setMotion(const ttstr &v) { _motion = v; }
        ttstr getChara() const { return _chara; }
        void setChara(const ttstr &v) { _chara = v; }
        tjs_int getTickCount() const { return _tickCount; }
        void setTickCount(tjs_int v) { _tickCount = v; }
        tjs_int getLastTime() const { return _lastTime; }
        void setLastTime(tjs_int v) { _lastTime = v; }
        tjs_real getSpeed() const { return _speed; }
        void setSpeed(tjs_real v) { _speed = v; }
        tjs_int getCompletionType() const { return _completionType; }
        void setCompletionType(tjs_int v) { _completionType = v; }

        void play(const ttstr &motion, tjs_int all = 0) {
            if(auto l = _logger()) l->info("Player::play motion={} all={}", motion.AsStdString(), all);
            _motion = motion;
            _allplaying = (all != 0);
            _playWasCalled = true;
            _stopCommandSent = false;
            _tickCount = 0;
            _lastTime = 0;
            auto newStorage = ResourceManager::getLastLoadedPath();
            if(!newStorage.IsEmpty()) {
                _loadedStorage = newStorage;
            }
            _psbImagesCached = false;
            _composited = false;
            _psbCacheRetries = 0;
            cleanupTempLayer();
            buildButtonBounds(_loadedStorage);

            auto lower = motion.AsLowerCase();
            auto lowerStd = lower.AsStdString();
            _isTransition = (lowerStd.compare(0, 4, "show") == 0 ||
                             lowerStd.compare(0, 4, "hide") == 0);
            // Steady-state motions don't need to "play" or send STOP.
            // All other motions (transitions, effects, etc.) need to play
            // and eventually send a STOP command so the KAG conductor can advance.
            bool isSteadyState = (lower == TJS_W("normal") || lower == TJS_W("status"));
            _playing = !isSteadyState;
            _stopCommandSent = isSteadyState;
        }
        void stop() {
            _playing = false;
            _allplaying = false;
        }
        void progress(tjs_int delta) {
            _tickCount += delta;
            if(_tickCount > _lastTime) _lastTime = _tickCount;
            if(_playing && _tickCount >= 100) {
                _playing = false;
                _allplaying = false;
            }
        }
        void clear(iTJSDispatch2 *target, tjs_int color) {
            _composited = false;
            if(!target) return;
            tTJSVariant width, height;
            if(TJS_SUCCEEDED(target->PropGet(0, TJS_W("width"), nullptr, &width, target)) &&
               TJS_SUCCEEDED(target->PropGet(0, TJS_W("height"), nullptr, &height, target))) {
                tTJSVariant args[5] = {
                    tTJSVariant((tjs_int)0),
                    tTJSVariant((tjs_int)0),
                    width,
                    height,
                    tTJSVariant(color),
                };
                tTJSVariant *argv[] = { &args[0], &args[1], &args[2], &args[3], &args[4] };
                target->FuncCall(0, TJS_W("fillRect"), nullptr, nullptr, 5, argv, target);
            }
        }

        void draw(iTJSDispatch2 *target) {
            if(!target) return;
            const ttstr storage = _loadedStorage.IsEmpty() ? ResourceManager::getLastLoadedPath()
                                                           : _loadedStorage;
            if(storage.IsEmpty()) return;

            auto logger = _logger();
            try {
                if(!_psbImagesCached) {
                    cachePSBImages(storage, logger);
                }

                if(!_psbImages.empty()) {
                    drawPSBImages(target, storage, logger);
                } else {
                    drawFallback(target, storage, logger);
                }
            } catch(const std::exception &e) {
                if(logger) logger->error("draw: exception: {}", e.what());
            } catch(...) {
                if(logger) logger->error("draw: unknown exception");
            }
        }

    private:
        struct PSBImageEntry {
            std::string key;
            ttstr path;
            int left = 0;
            int top = 0;
            int width = 0;
            int height = 0;
            int opacity = 255;
            bool isBackground = false;
        };

        void cachePSBImages(const ttstr &storage, const std::shared_ptr<spdlog::logger> &logger) {
            _psbImages.clear();

            auto *media = PSB::GetGlobalPSBMedia();
            if(!media) {
                if(logger) logger->warn("cachePSBImages: PSBMedia is null");
                return;
            }

            auto allLayerPositions = media->getLayerPositions(storage.AsStdString());
            const std::string charaStr = _chara.AsStdString();
            const std::string storageStr = storage.AsStdString();

            size_t matchCount = 0;
            for(const auto &lp : allLayerPositions) {
                if(lp.sceneName == charaStr) ++matchCount;
            }

            if(logger) logger->info("cachePSBImages: {} layer positions for {} (chara={}, total={})",
                matchCount, storageStr, charaStr, allLayerPositions.size());

            if(matchCount > 0) {
                std::set<std::string> seenKeys;
                for(const auto &lp : allLayerPositions) {
                    if(lp.sceneName != charaStr) continue;
                    if(lp.srcPath.empty()) continue;
                    if(!lp.visible) continue;
                    if(lp.srcPath.find("_over") != std::string::npos) continue;
                    if(lp.srcPath.find("_unselect") != std::string::npos) continue;
                    if(lp.srcPath.find("_press") != std::string::npos) continue;

                    std::string pngKey = storageStr + "/" + lp.srcPath + "/pixel.png";

                    // Dedup by image path + position so same image at different
                    // positions (e.g. repeated ON/OFF buttons) is kept
                    std::string dedupKey = pngKey + "@" +
                        std::to_string(static_cast<int>(lp.left)) + "," +
                        std::to_string(static_cast<int>(lp.top));
                    if(seenKeys.count(dedupKey)) continue;
                    seenKeys.insert(dedupKey);

                    ttstr path = ttstr(TJS_W("psb://")) + ttstr(pngKey.c_str());

                    PSB::PSBMedia::CachedImageInfo info;
                    bool hasInfo = media->getImageInfo(pngKey, info);
                    int w = hasInfo ? info.width : lp.width;
                    int h = hasInfo ? info.height : lp.height;
                    if(w <= 0 || h <= 0) continue;

                    if(!TVPIsExistentStorage(path)) continue;

                    PSBImageEntry img;
                    img.key = pngKey;
                    img.path = path;
                    img.left = _coordX + static_cast<int>(lp.left) - w / 2;
                    img.top = _coordY + static_cast<int>(lp.top) - h / 2;
                    img.width = w;
                    img.height = h;
                    img.opacity = lp.opacity;
                    img.isBackground = (lp.layerName.find("/bg") != std::string::npos ||
                                        lp.srcPath.find("/bg") != std::string::npos);
                    _psbImages.push_back(std::move(img));
                }
            }

            if(_psbImages.empty() && allLayerPositions.empty()) {
                // Raw fallback: only when PSB hasn't been parsed yet (no layer
                // position data at all). Once parsed, scenes that don't match
                // the current chara simply have no images to render — their
                // content is managed by the game script's Layer system.
                auto entries = media->getImagesByPrefix(storage.AsStdString());
                if(logger) logger->info("cachePSBImages: fallback rawEntries={} retry={}",
                    entries.size(), _psbCacheRetries);
                if(entries.empty()) {
                    _psbCacheRetries++;
                    if(_psbCacheRetries >= 5) {
                        _psbImagesCached = true;
                        if(logger) logger->warn("cachePSBImages: giving up after {} retries", _psbCacheRetries);
                    }
                    return;
                }

                for(auto &e : entries) {
                    if(e.info.width <= 0 || e.info.height <= 0) continue;
                    const auto &k = e.key;
                    if(k.find("/pixel") == std::string::npos) continue;
                    if(k.size() < 4 || k.substr(k.size() - 4) != ".png") continue;
                    if(k.find("_over/") != std::string::npos) continue;
                    if(k.find("_unselect/") != std::string::npos) continue;

                    PSBImageEntry img;
                    img.key = k;
                    img.path = ttstr(TJS_W("psb://")) + ttstr(k.c_str());
                    img.left = e.info.left;
                    img.top = e.info.top;
                    img.width = e.info.width;
                    img.height = e.info.height;
                    img.opacity = e.info.opacity;
                    img.isBackground = (k.find("/bg/") != std::string::npos);
                    if(TVPIsExistentStorage(img.path)) {
                        _psbImages.push_back(std::move(img));
                    }
                }
            } else if(_psbImages.empty()) {
                // Layer positions exist but none match current chara — this is
                // normal (e.g. MSGWIN in main.psb has no static images; its
                // content is composited by the game script at runtime)
                _psbImagesCached = true;
                return;
            }

            _psbImagesCached = true;
            _psbCacheRetries = 0;

            std::stable_sort(_psbImages.begin(), _psbImages.end(),
                [](const PSBImageEntry &a, const PSBImageEntry &b) {
                    auto bgPriority = [](const PSBImageEntry &e) -> int {
                        if(!e.isBackground) return 2;
                        // "title/icon/bg" (main bg) comes first
                        if(e.key.find("/title/") != std::string::npos) return 0;
                        return 1;
                    };
                    return bgPriority(a) < bgPriority(b);
                });

            if(logger) {
                logger->info("PSB cache: {} images for {}", _psbImages.size(), storage.AsStdString());
                for(auto &img : _psbImages) {
                    logger->info("  {} @ ({},{}) {}x{} opacity={}",
                                 img.key, img.left, img.top, img.width, img.height, img.opacity);
                }
            }

            if(_buttonBounds.empty()) {
                buildButtonBounds(storage);
            }
        }

        void drawPSBImages(iTJSDispatch2 *target, const ttstr &storage,
                           const std::shared_ptr<spdlog::logger> &logger) {
            if(_psbImages.empty()) return;

            // Skip re-compositing when images haven't changed since last draw
            if(_composited) return;

            iTJSDispatch2 *realLayer = resolveRealLayer(target);
            iTJSDispatch2 *tempParent = realLayer ? realLayer : target;

            tTJSVariant faceVal(static_cast<tjs_int>(0)); // dfAlpha
            target->PropSet(0, TJS_W("face"), nullptr, &faceVal, target);

            for(size_t i = 0; i < _psbImages.size(); i++) {
                const auto &img = _psbImages[i];

                iTJSDispatch2 *temp = getOrCreateTempLayer(tempParent);
                if(!temp) continue;

                if(!tryLoadImage(temp, img.path)) continue;

                tTJSVariant wVar, hVar;
                temp->PropGet(0, TJS_W("imageWidth"), nullptr, &wVar, temp);
                temp->PropGet(0, TJS_W("imageHeight"), nullptr, &hVar, temp);
                int iw = static_cast<int>(wVar.AsInteger());
                int ih = static_cast<int>(hVar.AsInteger());
                if(iw <= 0 || ih <= 0) continue;

                int opacity = std::min(img.opacity, 255);
                if(opacity <= 0) continue;

                tTJSVariant opArgs[9] = {
                    tTJSVariant(static_cast<tjs_int>(img.left)),
                    tTJSVariant(static_cast<tjs_int>(img.top)),
                    tTJSVariant(temp, temp),
                    tTJSVariant(static_cast<tjs_int>(0)),
                    tTJSVariant(static_cast<tjs_int>(0)),
                    tTJSVariant(static_cast<tjs_int>(iw)),
                    tTJSVariant(static_cast<tjs_int>(ih)),
                    tTJSVariant(static_cast<tjs_int>(2)),  // omAlpha
                    tTJSVariant(static_cast<tjs_int>(opacity)),
                };
                tTJSVariant *opArgv[] = { &opArgs[0], &opArgs[1], &opArgs[2],
                                          &opArgs[3], &opArgs[4], &opArgs[5],
                                          &opArgs[6], &opArgs[7], &opArgs[8] };
                try {
                    target->FuncCall(0, TJS_W("operateRect"), nullptr, nullptr, 9, opArgv, target);
                } catch(const std::exception &e) {
                    if(auto l = _logger()) l->warn("drawPSBImages: operateRect exception: {}", e.what());
                } catch(...) {
                    if(auto l = _logger()) l->warn("drawPSBImages: operateRect unknown exception");
                }
            }
            _composited = true;
        }

        void drawFallback(iTJSDispatch2 *target, const ttstr &storage,
                          const std::shared_ptr<spdlog::logger> &logger) {
            if(logger) logger->info("drawFallback: storage={} chara={} motion={}",
                storage.AsStdString(), _chara.AsStdString(), _motion.AsStdString());

            std::vector<ttstr> candidates;
            if(!_chara.IsEmpty() && !_motion.IsEmpty())
                candidates.emplace_back(TJS_W("motion/") + _chara + TJS_W("/") + _motion);
            if(!_chara.IsEmpty()) {
                candidates.emplace_back(TJS_W("motion/") + _chara + TJS_W("/normal"));
                candidates.emplace_back(TJS_W("motion/") + _chara + TJS_W("/show"));
            }
            candidates.emplace_back(TJS_W("source/title/motion/show"));
            candidates.emplace_back(TJS_W("source/title/motion/normal"));
            candidates.emplace_back(TJS_W("source/title/icon/bg/pixel"));

            for(const auto &c : candidates) {
                if(c.IsEmpty()) continue;
                const ttstr path = TJS_W("psb://") + storage + TJS_W("/") + c;
                if(logger) logger->debug("drawFallback: trying {}", path.AsStdString());
                if(tryLoadImage(target, path)) {
                    if(logger) logger->info("drawFallback: loaded {}", path.AsStdString());
                    return;
                }
                if(tryLoadImage(target, path + TJS_W(".png"))) {
                    if(logger) logger->info("drawFallback: loaded {}.png", path.AsStdString());
                    return;
                }
            }
            if(logger) logger->warn("drawFallback: no image loaded for {}", storage.AsStdString());
        }

        iTJSDispatch2 *resolveRealLayer(iTJSDispatch2 *target) {
            if(!target) return nullptr;
            auto *adaptor = ncbInstanceAdaptor<SeparateLayerAdaptor>::GetNativeInstance(target);
            if(adaptor) {
                auto *rt = adaptor->getTarget();
                if(rt) return rt;
                auto *owner = adaptor->getOwner();
                return owner;
            }
            return target;
        }

        iTJSDispatch2 *getOrCreateTempLayer(iTJSDispatch2 *realLayer) {
            if(_tempLayer) return _tempLayer;
            if(!realLayer) return nullptr;

            try {
                iTJSDispatch2 *global = TVPGetScriptDispatch();
                if(!global) return nullptr;

                tTJSVariant layerClassVar;
                global->PropGet(0, TJS_W("Layer"), nullptr, &layerClassVar, global);

                tTJSVariant windowVar;
                if(TJS_FAILED(realLayer->PropGet(0, TJS_W("window"), nullptr, &windowVar, realLayer))) {
                    global->Release();
                    return nullptr;
                }

                tTJSVariant ctorArgs[2] = { windowVar, tTJSVariant(realLayer, realLayer) };
                tTJSVariant *ctorArgv[] = { &ctorArgs[0], &ctorArgs[1] };
                iTJSDispatch2 *newLayer = nullptr;
                auto hr = layerClassVar.AsObjectNoAddRef()->CreateNew(
                    0, nullptr, nullptr, &newLayer, 2, ctorArgv, layerClassVar.AsObjectNoAddRef());
                global->Release();

                if(TJS_FAILED(hr) || !newLayer) return nullptr;

                tTJSVariant falseVar(false);
                newLayer->PropSet(TJS_MEMBERENSURE, TJS_W("visible"), nullptr, &falseVar, newLayer);

                _tempLayer = newLayer;
                return _tempLayer;
            } catch(...) {
                return nullptr;
            }
        }

        void cleanupTempLayer() {
            if(_tempLayer) {
                try {
                    _tempLayer->FuncCall(0, TJS_W("invalidate"), nullptr, nullptr, 0, nullptr, _tempLayer);
                } catch(...) {}
                _tempLayer->Release();
                _tempLayer = nullptr;
            }
        }

    public:
        void skipToSync() {}
        void setDrawAffineTranslateMatrix(tjs_real, tjs_real, tjs_real, tjs_real, tjs_real, tjs_real) {}
        void setCoord(tjs_real x, tjs_real y) {
            _coordX = x;
            _coordY = y;
        }

        bool contains(tjs_int x, tjs_int y) const {
            // When called via getLayerGetter → motion.contains, check specific button
            if(!_pendingButtonName.empty()) {
                std::string btn = _pendingButtonName;
                _pendingButtonName.clear();
                auto it = _buttonBounds.find(btn);
                if(it == _buttonBounds.end()) return false;
                const auto &b = it->second;
                return x >= b.left && x < b.left + b.width &&
                       y >= b.top && y < b.top + b.height;
            }
            // Layer-level hit test (from onHitTest): true if within layer bounds
            return true;
        }

        bool containsForButton(const ttstr &buttonName, tjs_int x, tjs_int y) const {
            auto it = _buttonBounds.find(buttonName.AsStdString());
            if(it == _buttonBounds.end()) return false;
            const auto &b = it->second;
            return x >= b.left && x < b.left + b.width &&
                   y >= b.top && y < b.top + b.height;
        }

        iTJSDispatch2 *getCommandList() {
            iTJSDispatch2 *arr = TJSCreateArrayObject();
            if(_playWasCalled && !_playing && !_stopCommandSent) {
                _stopCommandSent = true;
                iTJSDispatch2 *cmd = TJSCreateDictionaryObject();
                if(cmd) {
                    tTJSVariant typeVal(TJS_W("stop"));
                    cmd->PropSet(TJS_MEMBERENSURE, TJS_W("type"), nullptr, &typeVal, cmd);
                    tTJSVariant nameVal(_motion);
                    cmd->PropSet(TJS_MEMBERENSURE, TJS_W("name"), nullptr, &nameVal, cmd);
                    tTJSVariant cmdVar(cmd, cmd);
                    arr->PropSetByNum(TJS_MEMBERENSURE, 0, &cmdVar, arr);
                    cmd->Release();
                    if(auto l = _logger()) l->info("Player::getCommandList → STOP motion={}", _motion.AsStdString());
                }
                _isTransition = false;
            }
            return arr;
        }

        iTJSDispatch2 *createLayerGetter(iTJSDispatch2 *self, const ttstr &name) const {
            // Track the button being queried so contains() can do per-button hit testing
            _pendingButtonName = name.AsStdString();

            iTJSDispatch2 *obj = TJSCreateDictionaryObject();
            if(!obj) return nullptr;

            auto set = [&](const tjs_char *n, const tTJSVariant &value) {
                obj->PropSet(TJS_MEMBERENSURE, n, nullptr,
                             const_cast<tTJSVariant *>(&value), obj);
            };

            int left = 0, top = 0, width = 1, height = 1;
            auto it = _buttonBounds.find(name.AsStdString());
            if(it != _buttonBounds.end()) {
                left = it->second.left;
                top = it->second.top;
                width = it->second.width;
                height = it->second.height;
            }

            set(TJS_W("visible"), tTJSVariant(true));
            set(TJS_W("originX"), tTJSVariant(static_cast<tjs_real>(0)));
            set(TJS_W("originY"), tTJSVariant(static_cast<tjs_real>(0)));
            set(TJS_W("left"), tTJSVariant(static_cast<tjs_real>(left)));
            set(TJS_W("top"), tTJSVariant(static_cast<tjs_real>(top)));
            set(TJS_W("x"), tTJSVariant(static_cast<tjs_real>(left)));
            set(TJS_W("y"), tTJSVariant(static_cast<tjs_real>(top)));
            set(TJS_W("flipX"), tTJSVariant(false));
            set(TJS_W("flipY"), tTJSVariant(false));
            set(TJS_W("zoomX"), tTJSVariant(static_cast<tjs_real>(1)));
            set(TJS_W("zoomY"), tTJSVariant(static_cast<tjs_real>(1)));
            set(TJS_W("slantX"), tTJSVariant(static_cast<tjs_real>(0)));
            set(TJS_W("slantY"), tTJSVariant(static_cast<tjs_real>(0)));
            set(TJS_W("angleDeg"), tTJSVariant(static_cast<tjs_real>(0)));
            set(TJS_W("opacity"), tTJSVariant(static_cast<tjs_int>(255)));

            if(self) {
                tTJSVariant selfValue(self);
                set(TJS_W("motion"), selfValue);
            } else {
                set(TJS_W("motion"), tTJSVariant());
            }

            iTJSDispatch2 *shape = TJSCreateDictionaryObject();
            if(shape) {
                auto setShape = [&](const tjs_char *n, const tTJSVariant &value) {
                    shape->PropSet(TJS_MEMBERENSURE, n, nullptr,
                                   const_cast<tTJSVariant *>(&value), shape);
                };
                setShape(TJS_W("type"), tTJSVariant(static_cast<tjs_int>(2)));
                setShape(TJS_W("x"), tTJSVariant(static_cast<tjs_real>(left)));
                setShape(TJS_W("y"), tTJSVariant(static_cast<tjs_real>(top)));
                setShape(TJS_W("l"), tTJSVariant(static_cast<tjs_real>(left)));
                setShape(TJS_W("t"), tTJSVariant(static_cast<tjs_real>(top)));
                setShape(TJS_W("w"), tTJSVariant(static_cast<tjs_real>(width)));
                setShape(TJS_W("h"), tTJSVariant(static_cast<tjs_real>(height)));

                auto *containsFunc = new ShapeContainsFunc(left, top, width, height);
                tTJSVariant containsVar(containsFunc, containsFunc);
                shape->PropSet(TJS_MEMBERENSURE, TJS_W("contains"), nullptr,
                               &containsVar, shape);
                containsFunc->Release();

                tTJSVariant shapeValue(shape);
                set(TJS_W("shape"), shapeValue);
                shape->Release();
            } else {
                set(TJS_W("shape"), tTJSVariant());
            }

            return obj;
        }

        void setVariable(const ttstr &name, const tTJSVariant &value) {
            _variables[name.AsStdString()] = value;
        }
        tTJSVariant getVariable(const ttstr &name) const {
            auto it = _variables.find(name.AsStdString());
            if(it != _variables.end()) return it->second;
            return tTJSVariant();
        }

        void buildButtonBounds(const ttstr &storage) {
            _buttonBounds.clear();
            if(_psbImages.empty()) return;
            auto logger = _logger();

            // Method 1: title.psb style — match _nomal/_normal in image key
            for(auto &img : _psbImages) {
                if(img.width <= 0 || img.height <= 0) continue;
                if(img.key.find("_nomal/") == std::string::npos &&
                   img.key.find("_normal/") == std::string::npos) continue;

                auto iconStart = img.key.rfind("/icon/");
                if(iconStart == std::string::npos) continue;
                auto nameStart = iconStart + 6;
                auto nameSuffix = img.key.find("_no", nameStart);
                if(nameSuffix == std::string::npos) continue;
                auto iconName = img.key.substr(nameStart, nameSuffix - nameStart);
                auto btnName = "bt_" + iconName;

                ButtonBounds bounds;
                bounds.left = img.left;
                bounds.top = img.top;
                bounds.width = img.width;
                bounds.height = img.height;
                _buttonBounds[btnName] = bounds;

                if(logger) logger->info("Button bounds: {} → ({},{}) {}x{}",
                                        btnName, bounds.left, bounds.top, bounds.width, bounds.height);
            }

            if(!_buttonBounds.empty()) return;

            // Method 2: config.psb style — use PSBMedia button info with
            // stored image keys and position proximity for matching
            auto *media = PSB::GetGlobalPSBMedia();
            if(!media) return;

            std::string charaStr = _chara.AsStdString();
            auto allBtnBounds = media->getButtonBounds(storage.AsStdString());

            for(auto &btn : allBtnBounds) {
                if(!charaStr.empty() && btn.sceneName != charaStr) continue;
                if(_buttonBounds.count(btn.buttonName)) continue;

                const PSBImageEntry *bestImg = nullptr;
                float bestDist = 1e9f;
                for(auto &img : _psbImages) {
                    if(img.width <= 0 || img.height <= 0) continue;

                    bool keyMatch = false;
                    // Primary: match by stored imageKey (srcPath from PSB tree)
                    if(!btn.imageKey.empty() &&
                       img.key.find(btn.imageKey) != std::string::npos) {
                        keyMatch = true;
                    }

                    if(!keyMatch) {
                        // Fallback: match by button name substring in image key
                        auto iconPos = img.key.rfind("/icon/");
                        if(iconPos == std::string::npos) continue;
                        std::string iconPart = img.key.substr(iconPos + 6);
                        std::string btnLower = btn.buttonName;
                        for(auto &c : btnLower) { if(c >= 'A' && c <= 'Z') c += 32; }
                        std::string iconLower = iconPart;
                        for(auto &c : iconLower) { if(c >= 'A' && c <= 'Z') c += 32; }
                        if(iconLower.find(btnLower) == std::string::npos) continue;
                        keyMatch = true;
                    }

                    // When same image appears at multiple positions, pick the
                    // one closest to the button's PSB position
                    float imgPsbX = static_cast<float>(img.left + img.width / 2) - _coordX;
                    float imgPsbY = static_cast<float>(img.top + img.height / 2) - _coordY;
                    float dist = std::abs(imgPsbX - btn.left) + std::abs(imgPsbY - btn.top);
                    if(!bestImg || dist < bestDist) {
                        bestImg = &img;
                        bestDist = dist;
                    }
                }

                if(bestImg) {
                    ButtonBounds bounds;
                    bounds.left = bestImg->left;
                    bounds.top = bestImg->top;
                    bounds.width = bestImg->width;
                    bounds.height = bestImg->height;
                    _buttonBounds[btn.buttonName] = bounds;

                    if(logger) logger->info("Button bounds: {} (img={}) → ({},{}) {}x{}",
                                            btn.buttonName, btn.imageKey,
                                            bounds.left, bounds.top,
                                            bounds.width, bounds.height);
                }
            }
        }

    private:
        static bool tryLoadImage(iTJSDispatch2 *target, const ttstr &path) {
            if(!TVPIsExistentStorage(path)) return false;
            tTJSVariant arg(path);
            tTJSVariant *argv[] = { &arg };
            return TJS_SUCCEEDED(
                target->FuncCall(0, TJS_W("loadImages"), nullptr, nullptr, 1, argv, target));
        }

        inline static bool _useD3D = false;
        inline static bool _enableD3D = false;

        bool _playing = false;
        bool _allplaying = false;
        bool _playWasCalled = false;
        bool _isTransition = false;
        bool _stopCommandSent = false;
        mutable std::string _pendingButtonName;
        tjs_real _coordX = 0;
        tjs_real _coordY = 0;
        ttstr _motion;
        ttstr _chara;
        tjs_int _tickCount = 0;
        tjs_int _lastTime = 0;
        tjs_real _speed = 1.0;
        tjs_int _completionType = 0;
        ttstr _loadedStorage;

        bool _psbImagesCached = false;
        bool _composited = false;
        int _psbCacheRetries = 0;
        std::vector<PSBImageEntry> _psbImages;
        iTJSDispatch2 *_tempLayer = nullptr;

        struct ButtonBounds {
            int left = 0, top = 0, width = 0, height = 0;
        };
        std::unordered_map<std::string, ButtonBounds> _buttonBounds;

        std::unordered_map<std::string, tTJSVariant> _variables;
    };

} // namespace motion
