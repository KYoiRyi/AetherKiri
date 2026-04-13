#include "PSBMediaRegistry.h"

#include "PSBMedia.h"
#include "PSBValue.h"

namespace PSB {

    namespace {
        void registerValueResources(PSBMedia *psbMedia,
                                    const ttstr &normalizedContainer,
                                    const std::shared_ptr<IPSBValue> &value,
                                    std::vector<std::string> &path) {
            if(psbMedia == nullptr || value == nullptr) {
                return;
            }

            if(const auto resource = std::dynamic_pointer_cast<PSBResource>(value)) {
                ttstr resourceKey;
                for(size_t index = 0; index < path.size(); ++index) {
                    if(index != 0) {
                        resourceKey += TJS_W("/");
                    }
                    resourceKey += ttstr{ path[index] };
                }
                if(resourceKey.IsEmpty()) {
                    return;
                }
                psbMedia->NormalizePathName(resourceKey);
                psbMedia->add((normalizedContainer + TJS_W("/") + resourceKey)
                                  .AsStdString(),
                              resource);
                return;
            }

            if(const auto dic = std::dynamic_pointer_cast<PSBDictionary>(value)) {
                for(const auto &[key, child] : *dic) {
                    path.push_back(key);
                    registerValueResources(psbMedia, normalizedContainer, child, path);
                    path.pop_back();
                }
                return;
            }

            if(const auto list = std::dynamic_pointer_cast<PSBList>(value)) {
                for(size_t index = 0; index < list->size(); ++index) {
                    path.push_back(std::to_string(index));
                    registerValueResources(psbMedia, normalizedContainer,
                                           (*list)[static_cast<int>(index)],
                                           path);
                    path.pop_back();
                }
            }
        }

        void registerRootResourcesForContainer(
            PSBMedia *psbMedia, const ttstr &container,
            const std::shared_ptr<const PSBDictionary> &root) {
            if(psbMedia == nullptr || root == nullptr || container.IsEmpty()) {
                return;
            }

            ttstr normalizedContainer = container;
            psbMedia->NormalizeDomainName(normalizedContainer);

            std::vector<std::string> path;
            registerValueResources(
                psbMedia, normalizedContainer,
                std::const_pointer_cast<PSBDictionary>(root), path);
        }
    } // namespace

    void initPSBMedia() {}

    void deInitPSBMedia() {}

    void registerRootResources(const ttstr &container,
                               const std::shared_ptr<const PSBDictionary> &root) {
        registerRootResourcesForContainer(GetGlobalPSBMedia(), container, root);
    }

    void registerRootResources(const std::vector<ttstr> &containers,
                               const std::shared_ptr<const PSBDictionary> &root) {
        auto *psbMedia = GetGlobalPSBMedia();
        if(psbMedia == nullptr) {
            return;
        }
        for(const auto &container : containers) {
            registerRootResourcesForContainer(psbMedia, container, root);
        }
    }

    void registerRootResources(const ttstr &container, const PSBFile &file) {
        registerRootResources(container, file.getObjects());
    }

    void registerRootResources(const std::vector<ttstr> &containers,
                               const PSBFile &file) {
        registerRootResources(containers, file.getObjects());
    }
} // namespace PSB
