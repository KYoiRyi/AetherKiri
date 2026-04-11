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

static void ExtractImageInfo(const tTJSVariant& imgVar, tTJSVariant& src, tTJSVariant& sx, tTJSVariant& sy, tTJSVariant& sw, tTJSVariant& sh, tjs_real& img_l, tjs_real& img_t) {
    src = imgVar;
    sx = tTJSVariant(0);
    sy = tTJSVariant(0);
    sw = tTJSVariant(0);
    sh = tTJSVariant(0);
    img_l = 0; img_t = 0;

    if (imgVar.Type() == tvtObject) {
        iTJSDispatch2* dsp = imgVar.AsObjectNoAddRef();
        if (dsp) {
            tTJSVariant val;
            if (TJS_SUCCEEDED(dsp->PropGet(0, TJS_W("clipLeft"), NULL, &val, dsp))) sx = val;
            if (TJS_SUCCEEDED(dsp->PropGet(0, TJS_W("clipTop"), NULL, &val, dsp))) sy = val;
            if (TJS_SUCCEEDED(dsp->PropGet(0, TJS_W("clipWidth"), NULL, &val, dsp))) sw = val;
            if (TJS_SUCCEEDED(dsp->PropGet(0, TJS_W("clipHeight"), NULL, &val, dsp))) sh = val;

            if (TJS_SUCCEEDED(dsp->PropGet(0, TJS_W("imageLeft"), NULL, &val, dsp))) img_l = (tjs_real)val;
            if (TJS_SUCCEEDED(dsp->PropGet(0, TJS_W("imageTop"), NULL, &val, dsp))) img_t = (tjs_real)val;
        }
    }
}

// LayerExMulti Class Definition
class LayerExMulti {
public:
    static tjs_error multiAffineCopy(
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
            tjs_real img_l = 0, img_t = 0;
            ExtractImageInfo(entry.img, src, sx, sy, sw, sh, img_l, img_t);

            double final_a = a * entry.zoomX;
            double final_b = b * entry.zoomX;
            double final_c = c * entry.zoomY;
            double final_d = d * entry.zoomY;
            tjs_real local_cx = entry.x + img_l;
            tjs_real local_cy = entry.y + img_t;
            double final_tx = a * local_cx + c * local_cy + tx;
            double final_ty = b * local_cx + d * local_cy + ty;

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

    static tjs_error operateMultiAffine(
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
        bool clear = false;
        if (numparams >= 10 && param[9]->Type() != tvtVoid) clear = (int)*param[9] != 0;

        if (clear) {
            tTJSVariant val;
            tjs_uint32 color = 0;
            if (TJS_SUCCEEDED(objthis->PropGet(0, TJS_W("neutralColor"), NULL, &val, objthis))) color = (tjs_int)val;
            tTJSVariant args_clear[5];
            args_clear[0] = (tjs_int32)0; args_clear[1] = (tjs_int32)0; args_clear[2] = (tjs_int32)0; args_clear[3] = (tjs_int32)0; args_clear[4] = (tjs_int64)color;
            // IF WIDTH/HEIGHT ARE 0, THIS DOES NOTHING!
            tTJSVariant val_w, val_h;
            if (TJS_SUCCEEDED(objthis->PropGet(0, TJS_W("width"), NULL, &val_w, objthis))) args_clear[2] = val_w;
            if (TJS_SUCCEEDED(objthis->PropGet(0, TJS_W("height"), NULL, &val_h, objthis))) args_clear[3] = val_h;
            
            tTJSVariant* args_ptr[5] = {&args_clear[0], &args_clear[1], &args_clear[2], &args_clear[3], &args_clear[4]};
            objthis->FuncCall(0, TJS_W("fillRect"), NULL, NULL, 5, args_ptr, objthis);
        }

        for (const auto& entry : queue) {
            iTJSDispatch2* dsp = entry.img.AsObjectNoAddRef();
            if (!dsp) continue;

            tTJSVariant src = entry.img; // imgVar itself is the source layer!

            tTJSVariant val;
            tjs_real l = 0, t = 0, w = 0, h = 0;
            // The original multiimage plugin extracted clip bounds
            if (TJS_SUCCEEDED(dsp->PropGet(0, TJS_W("clipLeft"), NULL, &val, dsp))) l = val;
            if (TJS_SUCCEEDED(dsp->PropGet(0, TJS_W("clipTop"), NULL, &val, dsp))) t = val;
            if (TJS_SUCCEEDED(dsp->PropGet(0, TJS_W("clipWidth"), NULL, &val, dsp))) w = val;
            if (TJS_SUCCEEDED(dsp->PropGet(0, TJS_W("clipHeight"), NULL, &val, dsp))) h = val;

            tjs_real img_l = 0, img_t = 0;
            if (TJS_SUCCEEDED(dsp->PropGet(0, TJS_W("imageLeft"), NULL, &val, dsp))) img_l = val;
            if (TJS_SUCCEEDED(dsp->PropGet(0, TJS_W("imageTop"), NULL, &val, dsp))) img_t = val;

            tjs_real local_cx = entry.x + img_l;
            tjs_real local_cy = entry.y + img_t;
            double final_tx = a * local_cx + c * local_cy + tx;
            double final_ty = b * local_cx + d * local_cy + ty;

            tTJSVariant sx(l), sy(t), sw(w), sh(h);
            tTJSVariant is_affine(1);
            tTJSVariant var_a(a), var_b(b), var_c(c), var_d(d), var_tx(final_tx), var_ty(final_ty);
            tTJSVariant param_mode(mode), param_opa(opa), param_type(0); // 0 = stNearest

            tTJSVariant* args_ptr[15] = {&src, &sx, &sy, &sw, &sh, &is_affine, &var_a, &var_b, &var_c, &var_d, &var_tx, &var_ty, &param_mode, &param_opa, &param_type};
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

