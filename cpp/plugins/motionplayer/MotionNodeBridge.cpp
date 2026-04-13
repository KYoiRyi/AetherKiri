//
// TJS↔Native bridge helpers for MotionNode.
// Implements the helper methods declared in MotionNode.h.
// Separated to avoid circular dependency: MotionNode.h cannot include
// Player.h or ncbind.hpp, but these helpers need both.
//
// Aligned to libkrkr2.so:
//   - node+1912: shared_ptr<tTJSVariant> wrapping iTJSDispatch2 Player (nodeType=3)
//   - node+2296: tTJSVariant wrapping TJS Array of Players (nodeType=4)
//   - sub_6C1678: Array[i] + NativeInstanceSupport → native Player*
//   - sub_56C694: Array.count
//

#include "MotionNode.h"
#include "Player.h"
#include "ncbind.hpp"
#include "tjsArray.h"

namespace motion::detail {

    using PlayerAdaptor = ncbInstanceAdaptor<Player>;

    // --- nodeType=3: Motion child Player ---

    Player* MotionNode::getChildPlayer() const {
        // Aligned to sub_6BE0C0 (0x6BE22C..0x6BE260):
        // v15 = *v14 (dispatch ptr from node+1912)
        // if (!v15 || NIS fails || result==0) return nullptr
        // else return *(result+8) (native Player*)
        if (childPlayerVar.Type() != tvtObject) return nullptr;
        auto *dispatch = childPlayerVar.AsObjectNoAddRef();
        if (!dispatch) return nullptr;
        return PlayerAdaptor::GetNativeInstance(dispatch);
    }

    // --- nodeType=4: Particle children TJS Array ---

    static iTJSDispatch2* getArrayDispatch(const tTJSVariant &var) {
        if (var.Type() != tvtObject) return nullptr;
        return var.AsObjectNoAddRef();
    }

    int MotionNode::getParticleCount() const {
        // Aligned to sub_56C694: dispatch->PropGet(0, L"count", ...)
        auto *array = getArrayDispatch(particleArrayVar);
        if (!array) return 0;
        return TJSGetArrayElementCount(array);
    }

    Player* MotionNode::getParticleChild(int index) const {
        // Aligned to sub_6C1678 (0x6C1678):
        // 1. array->PropGetByNum(0, index, &elem, array)
        // 2. elem->NativeInstanceSupport(GETINSTANCE, ClassID, &nativePtr)
        // 3. return *(nativePtr+8)
        auto *array = getArrayDispatch(particleArrayVar);
        if (!array) return nullptr;
        tTJSVariant elem;
        if (TJS_FAILED(array->PropGetByNum(0, index, &elem, array)))
            return nullptr;
        if (elem.Type() != tvtObject) return nullptr;
        auto *dispatch = elem.AsObjectNoAddRef();
        if (!dispatch) return nullptr;
        return PlayerAdaptor::GetNativeInstance(dispatch);
    }

    iTJSDispatch2* MotionNode::getParticleChildDispatch(int index) const {
        auto *array = getArrayDispatch(particleArrayVar);
        if (!array) return nullptr;
        tTJSVariant elem;
        if (TJS_FAILED(array->PropGetByNum(0, index, &elem, array)))
            return nullptr;
        if (elem.Type() != tvtObject) return nullptr;
        return elem.AsObjectNoAddRef();
    }

    void MotionNode::addParticleChild(const tTJSVariant &playerVar) {
        // Aligned to TJS Array.add: dispatch->FuncCall(0, L"add", ...)
        auto *array = getArrayDispatch(particleArrayVar);
        if (!array) return;
        static tjs_uint addHint = 0;
        tTJSVariant val = playerVar;
        tTJSVariant *args[] = { &val };
        array->FuncCall(0, TJS_W("add"), &addHint, nullptr, 1, args, array);
    }

    void MotionNode::eraseParticleChild(int index) {
        // Aligned to sub_6C17A4 (0x6C1930): dispatch->FuncCall(0, L"erase", ...)
        auto *array = getArrayDispatch(particleArrayVar);
        if (!array) return;
        static tjs_uint eraseHint = 0;
        tTJSVariant idxVar(static_cast<tjs_int>(index));
        tTJSVariant *args[] = { &idxVar };
        array->FuncCall(0, TJS_W("erase"), &eraseHint, nullptr, 1, args, array);
    }

} // namespace motion::detail
