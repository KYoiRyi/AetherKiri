//
// Created by LiDon on 2025/9/11.
//

#include <algorithm>
#include <spdlog/spdlog.h>

#include "PSBMedia.h"

#include "PSBFile.h"
#include "resources/ImageMetadata.h"
#include "MsgIntf.h"
#include "Platform.h"
#include "SysInitIntf.h"
#include "UtilStreams.h"

namespace PSB {
#define LOGGER spdlog::get("plugin")

    namespace {
        size_t ClampSizeT(size_t value, size_t min_value, size_t max_value) {
            return std::max(min_value, std::min(value, max_value));
        }

        void RegisterPSBResourcesIntoMedia(
            PSBMedia &media, PSBFile &psb, const std::string &archiveKey) {
            const auto objs = psb.getObjects();
            if(objs) {
                for(const auto &[name, value] : *objs) {
                    const auto resource =
                        std::dynamic_pointer_cast<PSBResource>(value);
                    if(!resource)
                        continue;
                    media.add(archiveKey + "/" + name, resource);
                }
            }

            auto *handler = psb.getTypeHandler();
            if(!handler)
                return;
            auto resources = handler->collectResources(psb, false);
            for(auto &metadata : resources) {
                auto *image =
                    dynamic_cast<ImageMetadata *>(metadata.get());
                if(!image)
                    continue;
                auto resource = image->getResource();
                if(!resource)
                    continue;
                const std::string name = image->getName();
                if(name.empty())
                    continue;
                media.add(archiveKey + "/" + name, resource);
            }
        }
    } // namespace

    PSBMedia::PSBMedia() {
        _ref = 1;

        tTJSVariant val;
        if(TVPGetCommandLine(TJS_W("memory_profile"), &val)) {
            ttstr profile = ttstr(val).AsLowerCase();
            if(profile == TJS_W("aggressive") || profile == TJS_W("lowmem")) {
                _configuredMaxEntryCount = 1024;
                _configuredMaxByteSize = 128ULL * 1024ULL * 1024ULL;
            }
        }

        if(TVPGetCommandLine(TJS_W("psb_cache_entries"), &val)) {
            const tjs_int configured = static_cast<tjs_int>(val.AsInteger());
            if(configured > 0) {
                _configuredMaxEntryCount =
                    static_cast<size_t>(configured);
            }
        }
        if(TVPGetCommandLine(TJS_W("psb_cache_mb"), &val)) {
            const tjs_int configured = static_cast<tjs_int>(val.AsInteger());
            if(configured > 0) {
                _configuredMaxByteSize = static_cast<size_t>(configured) *
                    1024ULL * 1024ULL;
            }
        }

        _configuredMaxEntryCount =
            ClampSizeT(_configuredMaxEntryCount, 128, 8192);
        _configuredMaxByteSize = ClampSizeT(
            _configuredMaxByteSize, 16ULL * 1024ULL * 1024ULL,
            512ULL * 1024ULL * 1024ULL);
        _maxEntryCount = _configuredMaxEntryCount;
        _maxByteSize = _configuredMaxByteSize;
    }

    void PSBMedia::NormalizeDomainName(ttstr &name) {
        name = name.AsLowerCase();
    }

    void PSBMedia::NormalizePathName(ttstr &name) {
        auto *p = name.Independ();
        while(*p) {
            if(*p == TJS_W('\\')) {
                *p = TJS_W('/');
            }
            ++p;
        }
        name = name.AsLowerCase();
    }

    std::string PSBMedia::canonicalizeKey(const std::string &key) const {
        std::string out;
        out.reserve(key.size());
        for(const char ch : key) {
            if(ch == '\\') {
                out.push_back('/');
            } else if(ch >= 'A' && ch <= 'Z') {
                out.push_back(static_cast<char>(ch - 'A' + 'a'));
            } else {
                out.push_back(ch);
            }
        }

        constexpr const char *kFileScheme = "file://";
        if(out.rfind(kFileScheme, 0) == 0) {
            out.erase(0, 7);
            if(out.rfind("./", 0) == 0) {
                out.erase(0, 1);
            }
        }
        if(out.rfind("./", 0) == 0) {
            out.erase(0, 1);
        }

        std::string compact;
        compact.reserve(out.size());
        bool prevSlash = false;
        for(const char ch : out) {
            if(ch == '/') {
                if(prevSlash) {
                    continue;
                }
                prevSlash = true;
            } else {
                prevSlash = false;
            }
            compact.push_back(ch);
        }
        return compact;
    }

    void PSBMedia::touchLocked(CacheEntry &entry) {
        if(entry.lruIt != _lru.begin()) {
            _lru.splice(_lru.begin(), _lru, entry.lruIt);
            entry.lruIt = _lru.begin();
        }
    }

    void PSBMedia::adaptBudgetByMemoryPressureLocked() {
        const tjs_int self_used_mb = TVPGetSelfUsedMemory();
        const tjs_int free_mb = TVPGetSystemFreeMemory();

        size_t max_entry_count = _configuredMaxEntryCount;
        size_t max_byte_size = _configuredMaxByteSize;

        if((self_used_mb >= 1500) || (free_mb >= 0 && free_mb < 512)) {
            max_entry_count = std::min(max_entry_count, static_cast<size_t>(512));
            max_byte_size = std::min(
                max_byte_size, static_cast<size_t>(96ULL * 1024ULL * 1024ULL));
        } else if((self_used_mb >= 1100) || (free_mb >= 0 && free_mb < 800)) {
            max_entry_count = std::min(max_entry_count, static_cast<size_t>(768));
            max_byte_size = std::min(
                max_byte_size, static_cast<size_t>(144ULL * 1024ULL * 1024ULL));
        } else if((self_used_mb >= 850) || (free_mb >= 0 && free_mb < 1200)) {
            max_entry_count = std::min(max_entry_count, static_cast<size_t>(1024));
            max_byte_size = std::min(
                max_byte_size, static_cast<size_t>(192ULL * 1024ULL * 1024ULL));
        }

        _maxEntryCount = max_entry_count;
        _maxByteSize = max_byte_size;
    }

    PSBMedia::ResourceMap::iterator
    PSBMedia::findBySuffixLocked(const std::string &key) {
        for(auto it = _resources.begin(); it != _resources.end(); ++it) {
            const auto &stored = it->first;
            if(stored.size() < key.size()) {
                continue;
            }
            if(stored.compare(stored.size() - key.size(), key.size(), key) != 0) {
                continue;
            }
            if(stored.size() == key.size()) {
                return it;
            }

            const char boundary = stored[stored.size() - key.size() - 1];
            if(boundary == '/' || boundary == '>') {
                return it;
            }
        }
        return _resources.end();
    }

    void PSBMedia::evictIfNeededLocked() {
        adaptBudgetByMemoryPressureLocked();

        size_t evictedCount = 0;
        size_t evictedBytes = 0;
        while((_resources.size() > _maxEntryCount || _bytesInUse > _maxByteSize) &&
              !_lru.empty()) {
            const std::string victimKey = _lru.back();
            _lru.pop_back();

            auto it = _resources.find(victimKey);
            if(it == _resources.end()) {
                continue;
            }

            evictedCount++;
            evictedBytes += it->second.sizeBytes;
            _bytesInUse -= it->second.sizeBytes;
            _resources.erase(it);
        }

        if(evictedCount > 0) {
            LOGGER->debug(
                "PSB media cache evicted: count={} bytes={} remain_count={} "
                "remain_bytes={}",
                evictedCount, evictedBytes, _resources.size(), _bytesInUse);
        }
    }

    bool PSBMedia::CheckExistentStorage(const ttstr &name) {
        const auto key = canonicalizeKey(name.AsStdString());
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if(_resources.find(key) != _resources.end()) {
                _hitCount++;
                return true;
            }
            const bool found = findBySuffixLocked(key) != _resources.end();
            if(found)
                _hitCount++;
            else
                _missCount++;
            if(found)
                return true;
        }
        if(!tryLazyLoadArchive(key))
            return false;
        std::lock_guard<std::mutex> lock(_mutex);
        if(_resources.find(key) != _resources.end()) {
            _hitCount++;
            return true;
        }
        const bool found = findBySuffixLocked(key) != _resources.end();
        if(found)
            _hitCount++;
        else
            _missCount++;
        return found;
    }

    tTJSBinaryStream *PSBMedia::Open(const ttstr &name, tjs_uint32 flags) {
        (void)flags;
        const auto key = canonicalizeKey(name.AsStdString());

        std::shared_ptr<PSBResource> res;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            auto it = _resources.find(key);
            if(it == _resources.end()) {
                it = findBySuffixLocked(key);
                if(it != _resources.end()) {
                    LOGGER->debug("PSB media cache suffix-hit: {} -> {}", key,
                                  it->first);
                }
            }
            if(it != _resources.end() && it->second.resource != nullptr) {
                _hitCount++;
                touchLocked(it->second);
                res = it->second.resource;
            }
        }

        if(!res && tryLazyLoadArchive(key)) {
            std::lock_guard<std::mutex> lock(_mutex);
            auto it = _resources.find(key);
            if(it == _resources.end()) {
                it = findBySuffixLocked(key);
                if(it != _resources.end()) {
                    LOGGER->debug("PSB media cache suffix-hit(after-load): {} -> {}",
                                  key, it->first);
                }
            }
            if(it != _resources.end() && it->second.resource != nullptr) {
                _hitCount++;
                touchLocked(it->second);
                res = it->second.resource;
            }
        }

        if(!res) {
            std::lock_guard<std::mutex> lock(_mutex);
            _missCount++;
            LOGGER->warn("PSB media cache miss: {}", key);
            TVPThrowExceptionMessage(TJS_W("%1:cannot open psb resource"),
                                     name);
        }
        auto *memoryStream = new tTVPMemoryStream();
        memoryStream->WriteBuffer(res->data.data(), res->data.size());
        memoryStream->Seek(0, TJS_BS_SEEK_SET);
        return memoryStream;
    }

    bool PSBMedia::tryLazyLoadArchive(const std::string &key) {
        const auto slashPos = key.find('/');
        if(slashPos == std::string::npos || slashPos == 0)
            return false;

        const std::string archiveKey = key.substr(0, slashPos);
        bool shouldAttemptLoad = false;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            shouldAttemptLoad =
                _loadedArchives.insert(archiveKey).second;
        }
        if(!shouldAttemptLoad)
            return false;

        PSBFile psb;
        try {
            ttstr archivePath(archiveKey.c_str());
            if(!psb.loadPSBFile(archivePath)) {
                std::lock_guard<std::mutex> lock(_mutex);
                _loadedArchives.erase(archiveKey);
                LOGGER->debug("PSB lazy-load failed: {}", archiveKey);
                return false;
            }
            LOGGER->info("PSB lazy-load archive: {}", archiveKey);
            RegisterPSBResourcesIntoMedia(*this, psb, archiveKey);
            return true;
        } catch(const std::exception &e) {
            std::lock_guard<std::mutex> lock(_mutex);
            _loadedArchives.erase(archiveKey);
            LOGGER->warn("PSB lazy-load error: {} ({})", e.what(), archiveKey);
            return false;
        } catch(...) {
            std::lock_guard<std::mutex> lock(_mutex);
            _loadedArchives.erase(archiveKey);
            LOGGER->warn("PSB lazy-load unknown error: {}", archiveKey);
            return false;
        }
    }

    void PSBMedia::GetListAt(const ttstr &name, iTVPStorageLister *lister) {
        LOGGER->error("TODO: PSBMedia GetListAt");
    }

    void PSBMedia::GetLocallyAccessibleName(ttstr &name) {
        LOGGER->error("can't get GetLocallyAccessibleName from {}!",
                      name.AsStdString());
    }

    void PSBMedia::add(const std::string &name,
                       const std::shared_ptr<PSBResource> &resource) {
        if(resource == nullptr) {
            return;
        }

        const auto key = canonicalizeKey(name);
        const size_t incomingSize = resource->data.size();

        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _resources.find(key);
        if(it != _resources.end()) {
            _bytesInUse -= it->second.sizeBytes;
            it->second.resource = resource;
            it->second.sizeBytes = incomingSize;
            _bytesInUse += incomingSize;
            touchLocked(it->second);
        } else {
            _lru.push_front(key);
            _resources.emplace(key,
                               CacheEntry{ resource, incomingSize, _lru.begin() });
            _bytesInUse += incomingSize;
        }

        evictIfNeededLocked();
    }

    void PSBMedia::setCacheBudget(size_t maxEntries, size_t maxBytes) {
        std::lock_guard<std::mutex> lock(_mutex);
        _configuredMaxEntryCount = ClampSizeT(maxEntries, 128, 8192);
        _configuredMaxByteSize = ClampSizeT(
            maxBytes, 16ULL * 1024ULL * 1024ULL, 512ULL * 1024ULL * 1024ULL);
        _maxEntryCount = _configuredMaxEntryCount;
        _maxByteSize = _configuredMaxByteSize;
        evictIfNeededLocked();
    }

    PSBMediaCacheStats PSBMedia::getCacheStats() const {
        std::lock_guard<std::mutex> lock(_mutex);
        PSBMediaCacheStats stats;
        stats.entryCount = _resources.size();
        stats.entryLimit = _maxEntryCount;
        stats.bytesInUse = _bytesInUse;
        stats.byteLimit = _maxByteSize;
        stats.hitCount = _hitCount;
        stats.missCount = _missCount;
        return stats;
    }

    void PSBMedia::removeByPrefix(const std::string &prefix) {
        std::string normalizedPrefix = canonicalizeKey(prefix);
        if(!normalizedPrefix.empty() && normalizedPrefix.back() != '/') {
            normalizedPrefix.push_back('/');
        }

        std::lock_guard<std::mutex> lock(_mutex);
        for(auto it = _resources.begin(); it != _resources.end();) {
            if(it->first.rfind(normalizedPrefix, 0) == 0) {
                _bytesInUse -= it->second.sizeBytes;
                _lru.erase(it->second.lruIt);
                it = _resources.erase(it);
                continue;
            }
            ++it;
        }
    }

    void PSBMedia::clear() {
        std::lock_guard<std::mutex> lock(_mutex);
        _resources.clear();
        _lru.clear();
        _bytesInUse = 0;
        _hitCount = 0;
        _missCount = 0;
    }
} // namespace PSB
