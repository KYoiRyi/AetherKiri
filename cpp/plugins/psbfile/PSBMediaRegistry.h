#pragma once

#include <memory>
#include <vector>

#include "tjs.h"
#include "PSBFile.h"

namespace PSB {
    void initPSBMedia();
    void deInitPSBMedia();

    void registerRootResources(const ttstr &container,
                               const std::shared_ptr<const PSBDictionary> &root);
    void registerRootResources(const std::vector<ttstr> &containers,
                               const std::shared_ptr<const PSBDictionary> &root);

    void registerRootResources(const ttstr &container, const PSBFile &file);
    void registerRootResources(const std::vector<ttstr> &containers,
                               const PSBFile &file);
} // namespace PSB
