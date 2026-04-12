#include "ncbind.hpp"
#include <string>

// ----------------------------------------------------------------------------
// menu.dll Dummy Registration
// Prevents Plugins.link("menu.dll") from invoking standard fallback/mock behavior
// ----------------------------------------------------------------------------
static void menu_dll_stub() {}

#define NCB_MODULE_NAME TJS_W("menu.dll")
NCB_PRE_REGIST_CALLBACK(menu_dll_stub);
#undef NCB_MODULE_NAME

// ----------------------------------------------------------------------------
// MenuItem Additions
// Provides static shortcut parsers and a dummy HMENU for legacy scripts
// ----------------------------------------------------------------------------
class MenuItemExt {
public:
    static tjs_int textToKeycode(ttstr text) {
        return 0;
    }

    static ttstr keycodeToText(tjs_int keycode) {
        return ttstr(TJS_W(""));
    }

    static tjs_error TJS_INTF_METHOD getHMENU(tTJSVariant *result, tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis) {
        if (result) {
            *result = (tjs_int)0;
        }
        return TJS_S_OK;
    }
};

NCB_ATTACH_CLASS_WITH_HOOK(MenuItemExt, MenuItem) {
    NCB_METHOD(textToKeycode);
    NCB_METHOD(keycodeToText);
    NCB_PROPERTY_RAW_CALLBACK_RO(HMENU, getHMENU, 0);
}

// ----------------------------------------------------------------------------
// Window Additions
// Adds the 'menu' property to Window to persist references to root MenuItems.
// ----------------------------------------------------------------------------
class WindowMenuExt {
    iTJSDispatch2 *self;
    tTJSVariant menuVar;

public:
    WindowMenuExt(iTJSDispatch2 *obj) : self(obj) {}

    tTJSVariant getMenu() const {
        return menuVar;
    }

    void setMenu(const tTJSVariant& v) {
        menuVar = v;
    }
};

NCB_GET_INSTANCE_HOOK(WindowMenuExt) {
    NCB_GET_INSTANCE_HOOK_CLASS(){}
    ~NCB_GET_INSTANCE_HOOK_CLASS(){}
    NCB_INSTANCE_GETTER(objthis) { 
        ClassT *obj = GetNativeInstance(objthis);
        if(!obj) {
            SetNativeInstance(objthis, (obj = new ClassT(objthis)));
        }
        return obj;
    }
};

NCB_ATTACH_CLASS_WITH_HOOK(WindowMenuExt, Window) {
    NCB_PROPERTY(menu, getMenu, setMenu);
}
