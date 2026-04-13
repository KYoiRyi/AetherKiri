//
// Created by LiDon on 2025/9/15.
//
#pragma once
#include <memory>
#include <unordered_map>
#include "tjs.h"

namespace motion {

    class ResourceManager {
    public:
        ResourceManager();

        explicit ResourceManager(iTJSDispatch2 *kag, tjs_int cacheSize);

        tTJSVariant load(ttstr path) const;
        void unload(ttstr path) const;
        void clearCache() const;
        tTJSVariant getLastLoadedModule() const;
        tTJSVariant findLoaded(ttstr path) const;
        [[nodiscard]] static tjs_int getEmotePSBDecryptSeed();

        static tjs_error setEmotePSBDecryptSeed(tTJSVariant *r, tjs_int count,
                                                tTJSVariant **p,
                                                iTJSDispatch2 *obj);

        static tjs_error setEmotePSBDecryptFunc(tTJSVariant *r, tjs_int n,
                                                tTJSVariant **p,
                                                iTJSDispatch2 *obj);

    private:
        struct State {
            std::unordered_map<std::string, tTJSVariant> loadedModules;
            std::string lastLoadedPath;
            tTJSVariant lastLoadedModule;
        };

        std::shared_ptr<State> _state;
        inline static int _decryptSeed;
    };
} // namespace motion
