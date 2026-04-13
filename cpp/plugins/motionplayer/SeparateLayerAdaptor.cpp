#include "PlayerInternal.h"

using namespace motion::internal;

namespace {

    tTJSNI_BaseLayer *resolveNativeLayer(iTJSDispatch2 *layerObject) {
        if(!layerObject) {
            return nullptr;
        }
        tTJSNI_BaseLayer *layer = nullptr;
        if(TJS_FAILED(layerObject->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&layer))) || !layer) {
            return nullptr;
        }
        return layer;
    }

    iTJSDispatch2 *resolveAssignableLayer(const tTJSVariant &value) {
        if(value.Type() != tvtObject || !value.AsObjectNoAddRef()) {
            return nullptr;
        }

        if(auto *adaptor =
               ncbInstanceAdaptor<motion::SeparateLayerAdaptor>::GetNativeInstance(
                   value.AsObjectNoAddRef(), false)) {
            if(auto *privateTarget = adaptor->getPrivateRenderTargetObject()) {
                return privateTarget;
            }
            if(auto *target = tryResolveLayerDispatch(adaptor->getTargetLayer())) {
                return target;
            }
            return adaptor->getOwner();
        }

        return tryResolveLayerDispatch(value);
    }

    bool copyLayerImages(iTJSDispatch2 *sourceLayerObject,
                         iTJSDispatch2 *targetLayerObject) {
        if(!sourceLayerObject || !targetLayerObject) {
            return false;
        }

        try {
            tTJSVariant targetVar(targetLayerObject, targetLayerObject);
            tTJSVariant *args[] = { &targetVar };
            return TJS_SUCCEEDED(sourceLayerObject->FuncCall(
                0, TJS_W("assignImages"), nullptr, nullptr, 1, args,
                sourceLayerObject));
        } catch(...) {
            return false;
        }
    }

} // namespace

namespace motion {

    void SeparateLayerAdaptor::trackManagedTarget(const tTJSVariant &target) {
        if(target.Type() != tvtObject || !target.AsObjectNoAddRef()) {
            return;
        }
        const auto *ptr = target.AsObjectNoAddRef();
        for(const auto &existing : _managedTargets) {
            if(existing.Type() == tvtObject && existing.AsObjectNoAddRef() == ptr) {
                return;
            }
        }
        _managedTargets.push_back(target);
    }

    void SeparateLayerAdaptor::setPrivateRenderTarget(tTJSVariant v) {
        _privateRenderTarget = v;
        trackManagedTarget(v);
    }

    void SeparateLayerAdaptor::clearPrivateRenderState() {
        for(auto &target : _managedTargets) {
            if(target.Type() != tvtObject || !target.AsObjectNoAddRef()) {
                continue;
            }
            if(auto *layer = resolveNativeLayer(target.AsObjectNoAddRef())) {
                layer->SetVisible(false);
            }
            target.Clear();
        }
        _managedTargets.clear();
        _privateRenderTarget.Clear();
    }

    void SeparateLayerAdaptor::c() { clearPrivateRenderState(); }

    tjs_error SeparateLayerAdaptor::assignCompat(tTJSVariant *result,
                                                 tjs_int numparams,
                                                 tTJSVariant **param,
                                                 iTJSDispatch2 *objthis) {
        if(result) {
            result->Clear();
        }

        auto *nativeInstance =
            ncbInstanceAdaptor<SeparateLayerAdaptor>::GetNativeInstance(objthis, true);
        if(!nativeInstance) {
            return TJS_E_INVALIDOBJECT;
        }

        tTJSVariant sourceValue;
        if(numparams > 0 && param && param[0]) {
            sourceValue = *param[0];
        }

        iTJSDispatch2 *sourceLayerObject = nullptr;
        if(sourceValue.Type() == tvtObject && sourceValue.AsObjectNoAddRef()) {
            sourceLayerObject = resolveAssignableLayer(sourceValue);
        }
        if(!sourceLayerObject) {
            sourceLayerObject = nativeInstance->getPrivateRenderTargetObject();
        }

        tTJSVariant targetValue = nativeInstance->getTargetLayer();
        if(targetValue.Type() != tvtObject || !targetValue.AsObjectNoAddRef()) {
            targetValue = nativeInstance->getOwnerVariant();
        }
        iTJSDispatch2 *targetLayerObject = resolveAssignableLayer(targetValue);
        if(!targetLayerObject) {
            targetLayerObject = nativeInstance->getOwner();
        }

        if(!sourceLayerObject || !targetLayerObject) {
            return TJS_S_OK;
        }

        auto *sourceLayer = resolveNativeLayer(sourceLayerObject);
        auto *targetLayer = resolveNativeLayer(targetLayerObject);
        if(sourceLayer && targetLayer) {
            targetLayer->SetAbsoluteOrderMode(sourceLayer->GetAbsoluteOrderMode());
            targetLayer->SetOpacity(sourceLayer->GetOpacity());
            targetLayer->SetType(sourceLayer->GetType());
            targetLayer->SetVisible(sourceLayer->GetVisible());

            if(sourceLayer->GetWidth() > 0 && sourceLayer->GetHeight() > 0) {
                targetLayer->SetSize(sourceLayer->GetWidth(),
                                     sourceLayer->GetHeight());
            }
            if(sourceLayer->GetImageWidth() > 0 &&
               sourceLayer->GetImageHeight() > 0) {
                targetLayer->SetHasImage(true);
                targetLayer->SetImageSize(sourceLayer->GetImageWidth(),
                                          sourceLayer->GetImageHeight());
            }
        }

        copyLayerImages(sourceLayerObject, targetLayerObject);
        if(result) {
            *result = tTJSVariant(targetLayerObject, targetLayerObject);
        }
        return TJS_S_OK;
    }

} // namespace motion
