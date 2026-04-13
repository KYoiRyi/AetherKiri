//
// Build persistent node tree from PSB layer hierarchy.
// Aligned to libkrkr2.so sub_6B4A6C (0x6B4A6C).
//

#include "NodeTree.h"
#include "MotionNode.h"
#include "RuntimeSupport.h"
#include "Player.h"
#include "ncbind.hpp"
#include "tjsArray.h"
#include "psbfile/PSBFile.h"

#include <spdlog/spdlog.h>
#include <unordered_map>

namespace motion::detail {

    namespace {

        // PSB helper: extract a number from a dictionary key.
        std::optional<double>
        nodeTreePsbNumber(const std::shared_ptr<const PSB::PSBDictionary> &dic,
                          const char *key) {
            if (!dic) return std::nullopt;
            auto val = (*dic)[key];
            if (auto number = std::dynamic_pointer_cast<PSB::PSBNumber>(val)) {
                switch (number->numberType) {
                    case PSB::PSBNumberType::Float:
                        return number->getValue<float>();
                    case PSB::PSBNumberType::Double:
                        return number->getValue<double>();
                    case PSB::PSBNumberType::Int:
                        return static_cast<double>(number->getValue<int>());
                    case PSB::PSBNumberType::Long:
                    default:
                        return static_cast<double>(number->getValue<tjs_int64>());
                }
            }
            if (auto boolean = std::dynamic_pointer_cast<PSB::PSBBool>(val)) {
                return boolean->value ? 1.0 : 0.0;
            }
            return std::nullopt;
        }

        // PSB helper: extract a string from a dictionary key.
        std::string
        nodeTreePsbString(const std::shared_ptr<const PSB::PSBDictionary> &dic,
                          const char *key) {
            if (!dic) return {};
            if (auto text = std::dynamic_pointer_cast<PSB::PSBString>((*dic)[key])) {
                return text->value;
            }
            return {};
        }

        // PSB helper: extract a list from a dictionary key.
        std::shared_ptr<PSB::PSBList>
        nodeTreePsbList(const std::shared_ptr<const PSB::PSBDictionary> &dic,
                        const char *key) {
            if (!dic) return nullptr;
            return std::dynamic_pointer_cast<PSB::PSBList>((*dic)[key]);
        }

        // Check if any frame in frameList has a non-empty "src" in its "content".
        bool checkHasSource(const std::shared_ptr<const PSB::PSBDictionary> &node) {
            auto frameList = nodeTreePsbList(node, "frameList");
            if (!frameList) return false;
            for (int i = 0; i < static_cast<int>(frameList->size()); ++i) {
                auto frame = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                    (*frameList)[i]);
                if (!frame) continue;
                auto content = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                    (*frame)["content"]);
                if (!content) continue;
                auto srcVal = std::dynamic_pointer_cast<PSB::PSBString>(
                    (*content)["src"]);
                if (srcVal && !srcVal->value.empty()) return true;
            }
            return false;
        }

        // Recursively walk PSB layer tree, appending nodes to the flat vector.
        void walkTree(const std::shared_ptr<const PSB::PSBDictionary> &psbNode,
                      int parentIdx,
                      std::vector<MotionNode> &nodes) {
            if (!psbNode) return;

            MotionNode node;
            node.index = static_cast<int>(nodes.size());
            node.parentIndex = parentIdx;
            node.psbNode = psbNode;

            // "label" → layerName (node+0)
            node.layerName = nodeTreePsbString(psbNode, "label");

            // "type" → nodeType (node+28)
            // 0=obj, 1=shape, 3=motion, 4=particle, 5=camera, 6=emitter,
            // 7=shapeAABB, 9=camConstraint, 10=anchor, 12=stencilComposite
            if (auto v = nodeTreePsbNumber(psbNode, "type"))
                node.nodeType = static_cast<int>(*v);

            // "coordinate" → coordinateMode (node+24)
            if (auto v = nodeTreePsbNumber(psbNode, "coordinate"))
                node.coordinateMode = static_cast<int>(*v);

            // "inheritMask" → inheritFlags (node+40, default 0x1FC)
            if (auto v = nodeTreePsbNumber(psbNode, "inheritMask"))
                node.inheritFlags = static_cast<int>(*v);

            // "groundCorrection" → bool (node+47)
            if (auto v = nodeTreePsbNumber(psbNode, "groundCorrection"))
                node.groundCorrection = (*v != 0.0);

            // "transformOrder" → 4 ints (node+84..96, default [0,1,2,3])
            if (auto toList = nodeTreePsbList(psbNode, "transformOrder")) {
                for (int i = 0; i < 4 && i < static_cast<int>(toList->size()); ++i) {
                    if (auto v = std::dynamic_pointer_cast<PSB::PSBNumber>((*toList)[i])) {
                        switch (v->numberType) {
                            case PSB::PSBNumberType::Int:
                                node.transformOrder[i] = v->getValue<int>(); break;
                            default:
                                node.transformOrder[i] = static_cast<int>(v->getValue<tjs_int64>()); break;
                        }
                    }
                }
            }

            // "meshTransform" → meshType (node+2000, sub_6B3C78 at 0x6B4190)
            if (auto v = nodeTreePsbNumber(psbNode, "meshTransform"))
                node.meshType = static_cast<int>(*v);
            // "meshSyncChildMask" → meshFlags (node+2004, sub_6B3C78 at 0x6B41B8)
            if (auto v = nodeTreePsbNumber(psbNode, "meshSyncChildMask"))
                node.meshFlags = static_cast<int>(*v);
            // "meshDivision" → meshDivision (node+2008, sub_6B3C78 at 0x6B41D8)
            if (auto v = nodeTreePsbNumber(psbNode, "meshDivision"))
                node.meshDivision = static_cast<int>(*v);

            // "stencilType" → stencilType (node+52)
            if (auto v = nodeTreePsbNumber(psbNode, "stencilType")) {
                node.stencilTypeBase = static_cast<int>(*v);
                node.stencilType = node.stencilTypeBase;
            }

            // "shape" → shapeType (node+32, sub_6B3C78 case 1)
            if (auto v = nodeTreePsbNumber(psbNode, "shape"))
                node.shapeType = static_cast<int>(*v);

            // "anchor" → anchorType/cameraConstraintType (node+2376, sub_6B3C78 case 9)
            if (auto v = nodeTreePsbNumber(psbNode, "anchor"))
                node.anchorType = node.cameraConstraintType = static_cast<int>(*v);

            // Particle properties (sub_6B3C78 case 4, 0x6B4438..0x6B45E4)
            if (auto v = nodeTreePsbNumber(psbNode, "particle"))
                node.particleType = static_cast<int>(*v);
            if (auto v = nodeTreePsbNumber(psbNode, "particleMaxNum"))
                node.particleMaxNum = static_cast<int>(*v);
            if (auto v = nodeTreePsbNumber(psbNode, "particleAccelRatio"))
                node.particleAccelRatio = *v;
            if (auto v = nodeTreePsbNumber(psbNode, "particleInheritAngle"))
                node.particleInheritAngle = (*v != 0.0);
            if (auto v = nodeTreePsbNumber(psbNode, "particleInheritVelocity"))
                node.particleInheritVelocity = static_cast<int>(*v);
            if (auto v = nodeTreePsbNumber(psbNode, "particleFlyDirection"))
                node.particleFlyDirection = static_cast<int>(*v);
            if (auto v = nodeTreePsbNumber(psbNode, "particleApplyZoomToVelocity"))
                node.particleApplyZoomToVelocity = static_cast<int>(*v);
            if (auto v = nodeTreePsbNumber(psbNode, "particleDeleteOutsideScreen"))
                node.particleDeleteOutside = (*v != 0.0);
            if (auto v = nodeTreePsbNumber(psbNode, "particleTriVolume"))
                node.particleTriVolume = (*v != 0.0);
            // Binary: node+2192 is ONE field, used as both accel decay ratio
            // and camera damping. "particleCameraDamping" PSB key overwrites
            // "particleAccelRatio" if both present (same binary offset).
            if (auto v = nodeTreePsbNumber(psbNode, "particleCameraDamping"))
                node.particleAccelRatio = *v;

            // Check if any frame has a source image
            node.hasSource = checkHasSource(psbNode);

            // "emoteEdit" → emoteEditDict (node+1980, sub_6B3C78 at 0x6B3D48)
            if (auto ee = std::dynamic_pointer_cast<PSB::PSBDictionary>((*psbNode)["emoteEdit"]))
                node.emoteEditDict = ee;

            // === TJS↔Native bridge: create child objects (sub_6B3C78 case 3/4) ===
            if (node.nodeType == 3) {
                // Aligned to sub_6B3C78 case 3 (0x6B43C0..0x6B46E0):
                // operator new(0x568) → Player constructor → sub_6F1794 (NCB CreateAdaptor)
                // → store as tTJSVariant at node+1912.
                using PlayerAdaptor = ncbInstanceAdaptor<Player>;
                auto *childNative = new Player(ResourceManager{});
                if (auto *dispatch = PlayerAdaptor::CreateAdaptor(childNative)) {
                    node.childPlayerVar = tTJSVariant(dispatch, dispatch);
                    dispatch->Release();
                } else {
                    delete childNative;
                }
            } else if (node.nodeType == 4) {
                // Aligned to sub_6B3C78 case 4 (0x6B45D8..0x6B45E4):
                // sub_704CB8 (TJSCreateArrayObject) → store at node+2296.
                if (auto *array = TJSCreateArrayObject()) {
                    node.particleArrayVar = tTJSVariant(array, array);
                    array->Release();
                }
            }

            // Add this node to the flat vector
            const int thisIdx = node.index;
            nodes.push_back(std::move(node));

            // Recurse into "children"
            auto children = nodeTreePsbList(psbNode, "children");
            if (children) {
                for (int i = 0; i < static_cast<int>(children->size()); ++i) {
                    auto child = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                        (*children)[i]);
                    walkTree(child, thisIdx, nodes);
                }
            }
        }

    } // anonymous namespace

    std::vector<MotionNode> buildNodeTree(
        const MotionSnapshot &snapshot,
        const std::string &clipLabel) {

        std::vector<MotionNode> nodes;
        // Aligned to Player_buildNodeTree (0x6B51F0): root index 0 is a
        // synthetic container node, and all PSB layers are attached under it.
        MotionNode root;
        root.index = 0;
        root.parentIndex = -1;
        nodes.push_back(std::move(root));

        // Determine which layer set to use: current clip content first, then
        // fall back to snapshot root-level layers. This matches libkrkr2.so
        // using player+528 as the active motion/clip content object before
        // reading its "layer" property in Player_buildNodeTree (0x6B51F0).
        const std::unordered_map<std::string,
            std::shared_ptr<const PSB::PSBDictionary>> *layersByName = nullptr;
        const std::vector<std::string> *layerNames = nullptr;

        if (!clipLabel.empty()) {
            auto clipIt = snapshot.clipsByLabel.find(clipLabel);
            if (clipIt != snapshot.clipsByLabel.end()) {
                layersByName = &clipIt->second.layersByName;
                layerNames = &clipIt->second.layerNames;
            }
        }

        if (!layersByName) {
            layersByName = &snapshot.layersByName;
            layerNames = &snapshot.layerNames;
        }

        if (!layerNames || layerNames->empty()) {
            return nodes;
        }

        // Aligned to Player_buildNodeTree_recursive(player, 0, layerArray):
        // every top-level PSB layer uses the synthetic root (index 0) as parent.
        for (const auto &name : *layerNames) {
            auto it = layersByName->find(name);
            if (it == layersByName->end()) continue;
            walkTree(it->second, 0, nodes);
        }

        std::unordered_map<std::string, int> nodeIndexByLabel;
        nodeIndexByLabel.reserve(nodes.size());
        for(const auto &node : nodes) {
            if(!node.layerName.empty()) {
                nodeIndexByLabel.emplace(node.layerName, node.index);
            }
        }

        // Aligned to Player_buildNodeTree post-pass (0x6B51F0..0x6B55AC):
        // type==12 nodes with stencilType bit 2 set walk
        // "stencilCompositeMaskLayerList", resolve label→node, and set node+1961
        // on the referenced mask layers.
        for(auto &node : nodes) {
            if(node.nodeType != 12 || (node.stencilType & 4) == 0 || !node.psbNode) {
                continue;
            }
            auto maskLayers = nodeTreePsbList(
                node.psbNode, "stencilCompositeMaskLayerList");
            if(!maskLayers) {
                continue;
            }
            for(int i = 0; i < static_cast<int>(maskLayers->size()); ++i) {
                auto label = std::dynamic_pointer_cast<PSB::PSBString>((*maskLayers)[i]);
                if(!label || label->value.empty()) {
                    continue;
                }
                auto it = nodeIndexByLabel.find(label->value);
                if(it == nodeIndexByLabel.end()) {
                    continue;
                }
                auto &target = nodes[it->second];
                if(target.nodeType == 0 || target.nodeType == 3) {
                    target.stencilCompositeMaskReferenced = true;
                }
            }
        }

        if (auto logger = spdlog::get("plugin")) {
            logger->debug("buildNodeTree: clipLabel='{}', rootLayers={}, {} nodes built",
                          clipLabel, layerNames->size(), nodes.size());
        }

        return nodes;
    }

} // namespace motion::detail
