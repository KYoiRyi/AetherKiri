//
// Created by LiDon on 2025/9/11.
//
#pragma once

#include <list>
#include <mutex>
#include <unordered_set>
#include <unordered_map>

#include "PSBValue.h"
#include "StorageIntf.h"

namespace PSB {
    struct PSBMediaCacheStats {
        size_t entryCount = 0;
        size_t entryLimit = 0;
        size_t bytesInUse = 0;
        size_t byteLimit = 0;
        uint64_t hitCount = 0;
        uint64_t missCount = 0;
    };

    class PSBMedia;
    bool GetPSBMediaCacheStats(PSBMediaCacheStats &outStats);
    void SetPSBMediaCacheBudget(size_t maxEntries, size_t maxBytes);

    class PSBMedia : public iTVPStorageMedia {
    public:
        PSBMedia();

        ~PSBMedia() override = default;

        void AddRef() override { _ref++; }

        void Release() override {
            if(_ref == 1)
                delete this;
            else
                _ref--;
        }

        void GetName(ttstr &name) override { name = TJS_W("psb"); }

        void NormalizeDomainName(ttstr &name) override;

        void NormalizePathName(ttstr &name) override;

        bool CheckExistentStorage(const ttstr &name) override;

        tTJSBinaryStream *Open(const ttstr &name, tjs_uint32 flags) override;

        void GetListAt(const ttstr &name, iTVPStorageLister *lister) override;

        void GetLocallyAccessibleName(ttstr &name) override;

        void add(const std::string &name,
                 const std::shared_ptr<PSBResource> &resource);
        void removeByPrefix(const std::string &prefix);
        void clear();
        void setCacheBudget(size_t maxEntries, size_t maxBytes);
        PSBMediaCacheStats getCacheStats() const;

    private:
        struct CacheEntry {
            std::shared_ptr<PSBResource> resource;
            size_t sizeBytes = 0;
            std::list<std::string>::iterator lruIt;
        };
        using ResourceMap = std::unordered_map<std::string, CacheEntry>;

        std::string canonicalizeKey(const std::string &key) const;
        ResourceMap::iterator findBySuffixLocked(const std::string &key);
        bool tryLazyLoadArchive(const std::string &key);
        void touchLocked(CacheEntry &entry);
        void adaptBudgetByMemoryPressureLocked();
        void evictIfNeededLocked();

        int _ref = 0;
        mutable std::mutex _mutex;
        ResourceMap _resources;
        std::list<std::string> _lru;
        size_t _bytesInUse = 0;
        size_t _configuredMaxEntryCount = 2048;
        size_t _configuredMaxByteSize = 192ULL * 1024ULL * 1024ULL;
        size_t _maxEntryCount = 2048;
        size_t _maxByteSize = 192ULL * 1024ULL * 1024ULL;
        uint64_t _hitCount = 0;
        uint64_t _missCount = 0;
        std::unordered_set<std::string> _loadedArchives;
    };
} // namespace PSB
