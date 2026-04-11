#include "ncbind.hpp"
#include <vector>

#define NCB_MODULE_NAME TJS_W("multiimage.dll")

struct MultiImageEntry {
    tTJSVariant img;
    int x;
    int y;
    double zoomX;
    double zoomY;
    int time;
};

// MultiImage Class Definition
class MultiImage {
    std::vector<MultiImageEntry> queue;
public:
    MultiImage() {}
    ~MultiImage() {}

    void clear() {
        queue.clear();
    }

    void addZoom(double zoomX, double zoomY, int time) {
        if (!queue.empty()) {
            queue.back().zoomX = zoomX;
            queue.back().zoomY = zoomY;
            queue.back().time = time;
        }
    }

    void push(tTJSVariant img, int x, int y) {
        MultiImageEntry entry;
        entry.img = img;
        entry.x = x;
        entry.y = y;
        entry.zoomX = 1.0;
        entry.zoomY = 1.0;
        entry.time = 0;
        queue.push_back(entry);
    }

    void divisionLayer(double div) { }

    tTJSVariant calcMultiAffine(double a, double b, double c, double d, double tx, double ty, int w, int h, int opa, int mode) {
        return tTJSVariant(); 
    }

    tTJSVariant count() {
        return tTJSVariant((tjs_int)queue.size());
    }

    const std::vector<MultiImageEntry>& getQueue() const {
        return queue;
    }
};

NCB_REGISTER_CLASS(MultiImage)
{
    NCB_CONSTRUCTOR(());
    NCB_METHOD(clear);
    NCB_METHOD(addZoom);
    NCB_METHOD(push);
    NCB_METHOD(divisionLayer);
    NCB_METHOD(calcMultiAffine);
    NCB_PROPERTY_RO(count, count);
}

static void ExtractImageInfo(const tTJSVariant& imgVar, tTJSVariant& src, tTJSVariant& sx, tTJSVariant& sy, tTJSVariant& sw, tTJSVariant& sh) {
    src = imgVar;
    sx = tTJSVariant(0);
    sy = tTJSVariant(0);
    sw = tTJSVariant(0);
    sh = tTJSVariant(0);

    if (imgVar.Type() == tvtObject) {
        iTJSDispatch2* dsp = imgVar.AsObjectNoAddRef();
        if (dsp) {
            tTJSVariant val;
            if (TJS_SUCCEEDED(dsp->PropGet(0, TJS_W("src"), NULL, &val, dsp))) {
                src = val;
                if (TJS_SUCCEEDED(dsp->PropGet(0, TJS_W("sleft"), NULL, &val, dsp))) sx = val;
                if (TJS_SUCCEEDED(dsp->PropGet(0, TJS_W("stop"), NULL, &val, dsp))) sy = val;
                if (TJS_SUCCEEDED(dsp->PropGet(0, TJS_W("swidth"), NULL, &val, dsp))) sw = val;
                if (TJS_SUCCEEDED(dsp->PropGet(0, TJS_W("sheight"), NULL, &val, dsp))) sh = val;
            } else {
                if (TJS_SUCCEEDED(dsp->PropGet(0, TJS_W("width"), NULL, &val, dsp))) sw = val;
                if (TJS_SUCCEEDED(dsp->PropGet(0, TJS_W("height"), NULL, &val, dsp))) sh = val;
            }
        }
    }
}

// LayerExMulti Class Definition
class LayerExMulti {
public:
    static tjs_error TJS_INTF_METHOD multiAffineCopy(
        tTJSVariant *result, tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis) {

        if(numparams < 10 || !objthis) return TJS_E_BADPARAMCOUNT;
        
        tTJSVariant multiImg = *param[0];
        double a = (double)*param[1];
        double b = (double)*param[2];
        double c = (double)*param[3];
        double d = (double)*param[4];
        double tx = (double)*param[5];
        double ty = (double)*param[6];
        int opa = (int)*param[7];
        int mode = (int)*param[8];
        int type = (int)*param[9];

        if (multiImg.Type() != tvtObject) return TJS_S_OK;
        iTJSDispatch2* miDsp = multiImg.AsObjectNoAddRef();
        if (!miDsp) return TJS_S_OK;

        MultiImage* mi = ncbInstanceAdaptor<MultiImage>::GetNativeInstance(miDsp);
        if (!mi) return TJS_S_OK;

        const auto& queue = mi->getQueue();
        for (const auto& entry : queue) {
            tTJSVariant src, sx, sy, sw, sh;
            ExtractImageInfo(entry.img, src, sx, sy, sw, sh);

            double final_a = a * entry.zoomX;
            double final_b = b * entry.zoomX;
            double final_c = c * entry.zoomY;
            double final_d = d * entry.zoomY;
            double final_tx = a * entry.x + c * entry.y + tx;
            double final_ty = b * entry.x + d * entry.y + ty;

            tTJSVariant args[15];
            args[0] = src;
            args[1] = sx;
            args[2] = sy;
            args[3] = sw;
            args[4] = sh;
            args[5] = tTJSVariant((tjs_int)1); // affine mode
            args[6] = tTJSVariant(final_a);
            args[7] = tTJSVariant(final_b);
            args[8] = tTJSVariant(final_c);
            args[9] = tTJSVariant(final_d);
            args[10] = tTJSVariant(final_tx);
            args[11] = tTJSVariant(final_ty);
            args[12] = tTJSVariant(type);
            args[13] = tTJSVariant(opa);
            args[14] = tTJSVariant((tjs_int)0); // clear

            tTJSVariant* args_ptr[15] = {&args[0], &args[1], &args[2], &args[3], &args[4], &args[5], &args[6], &args[7], &args[8], &args[9], &args[10], &args[11], &args[12], &args[13], &args[14]};
            objthis->FuncCall(0, TJS_W("affineCopy"), NULL, NULL, 15, args_ptr, objthis);
        }

        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD operateMultiAffine(
        tTJSVariant *result, tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis) {
        
        if(numparams < 9 || !objthis) return TJS_E_BADPARAMCOUNT;
        
        tTJSVariant multiImg = *param[0];
        double a = (double)*param[1];
        double b = (double)*param[2];
        double c = (double)*param[3];
        double d = (double)*param[4];
        double tx = (double)*param[5];
        double ty = (double)*param[6];
        int opa = (int)*param[7];
        int mode = (int)*param[8];

        if (multiImg.Type() != tvtObject) return TJS_S_OK;
        iTJSDispatch2* miDsp = multiImg.AsObjectNoAddRef();
        if (!miDsp) return TJS_S_OK;

        MultiImage* mi = ncbInstanceAdaptor<MultiImage>::GetNativeInstance(miDsp);
        if (!mi) return TJS_S_OK;

        const auto& queue = mi->getQueue();
        for (const auto& entry : queue) {
            tTJSVariant src, sx, sy, sw, sh;
            ExtractImageInfo(entry.img, src, sx, sy, sw, sh);

            double final_a = a * entry.zoomX;
            double final_b = b * entry.zoomX;
            double final_c = c * entry.zoomY;
            double final_d = d * entry.zoomY;
            double final_tx = a * entry.x + c * entry.y + tx;
            double final_ty = b * entry.x + d * entry.y + ty;

            tTJSVariant args[15];
            args[0] = src;
            args[1] = sx;
            args[2] = sy;
            args[3] = sw;
            args[4] = sh;
            args[5] = tTJSVariant((tjs_int)1); // affine mode
            args[6] = tTJSVariant(final_a);
            args[7] = tTJSVariant(final_b);
            args[8] = tTJSVariant(final_c);
            args[9] = tTJSVariant(final_d);
            args[10] = tTJSVariant(final_tx);
            args[11] = tTJSVariant(final_ty);
            args[12] = tTJSVariant(mode); // mode
            args[13] = tTJSVariant(opa);
            args[14] = tTJSVariant((tjs_int)0); // type (default 0 nearest)

            tTJSVariant* args_ptr[15] = {&args[0], &args[1], &args[2], &args[3], &args[4], &args[5], &args[6], &args[7], &args[8], &args[9], &args[10], &args[11], &args[12], &args[13], &args[14]};
            objthis->FuncCall(0, TJS_W("operateAffine"), NULL, NULL, 15, args_ptr, objthis);
        }

        return TJS_S_OK;
    }
};

NCB_ATTACH_CLASS(LayerExMulti, Layer)
{
    RawCallback("multiAffineCopy", &LayerExMulti::multiAffineCopy, 0);
    RawCallback("operateMultiAffine", &LayerExMulti::operateMultiAffine, 0);
}

