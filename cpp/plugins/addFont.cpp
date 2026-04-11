#include "ncbind.hpp"

#include <vector>
#include "FontImpl.h"
using namespace std;

#define NCB_MODULE_NAME TJS_W("addFont.dll")

struct FontEx {
    /**
     * プライベートフォントの追加
     * @param fontFileName フォントファイル名
     * @param extract アーカイブからテンポラリ展開する
     * @return void:ファイルを開くのに失敗 0:フォント登録に失敗
     * 数値:登録したフォントの数
     */
    static tjs_error addFont(tTJSVariant *result, tjs_int numparams,
                             tTJSVariant **param, iTJSDispatch2 *objthis) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        ttstr filename = TVPGetPlacedPath(*param[0]);
        if(filename.length()) {
            int ret = TVPEnumFontsProc(filename);
            if(result) {
                *result = (int)ret;
            }
            return TJS_S_OK;
        }
        return TJS_S_OK;
    }
};

class tAddFontCompatFunction : public tTJSDispatch {
public:
    tjs_error FuncCall(tjs_uint32 flag, const tjs_char *membername,
                       tjs_uint32 *hint, tTJSVariant *result,
                       tjs_int numparams, tTJSVariant **param,
                       iTJSDispatch2 *objthis) override {
        if(membername)
            return TJS_E_MEMBERNOTFOUND;
        return FontEx::addFont(result, numparams, param, objthis);
    }
};

static void RegisterGlobalAddFontAlias(const tjs_char *name) {
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(!global)
        return;

    iTJSDispatch2 *func = new tAddFontCompatFunction();
    tTJSVariant val(func);
    func->Release();
    global->PropSet(TJS_MEMBERENSURE, name, nullptr, &val, global);
    global->Release();
}

static void PostRegistCallback() {
    RegisterGlobalAddFontAlias(TJS_W("AddTrueTypeFont"));
    RegisterGlobalAddFontAlias(TJS_W("AddFont"));
}

NCB_POST_REGIST_CALLBACK(PostRegistCallback);

// フックつきアタッチ
NCB_ATTACH_CLASS(FontEx, System) {
    RawCallback("addFont", &FontEx::addFont, TJS_STATICMEMBER);
    // Compatibility alias used by some older startup/font loader scripts.
    RawCallback("AddTrueTypeFont", &FontEx::addFont, TJS_STATICMEMBER);
    RawCallback("AddFont", &FontEx::addFont, TJS_STATICMEMBER);
}
