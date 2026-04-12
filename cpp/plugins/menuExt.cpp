#include "ncbind.hpp"
#include <string>

// ----------------------------------------------------------------------------
// menu.dll Dummy Registration
// Prevents Plugins.link("menu.dll") from invoking standard fallback/mock behavior
// ----------------------------------------------------------------------------
static void menu_dll_stub() {}

#define NCB_MODULE_NAME TJS_W("menu.dll")
NCB_PRE_REGIST_CALLBACK(menu_dll_stub);

// ----------------------------------------------------------------------------
// MenuItem Additions
// Provides static shortcut parsers and a dummy HMENU for legacy scripts
// ----------------------------------------------------------------------------
class MenuItemExt {
public:
    static tjs_error textToKeycode(tTJSVariant *result, tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis) {
        if (result) *result = 0;
        return TJS_S_OK;
    }

    static tjs_error keycodeToText(tTJSVariant *result, tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis) {
        if (result) *result = TJS_W("");
        return TJS_S_OK;
    }

    tTJSVariant getHMENU() const {
        return tTJSVariant(0);
    }

    void setHMENU(tTJSVariant v) {}
};

NCB_GET_INSTANCE_HOOK(MenuItemExt) {
    NCB_GET_INSTANCE_HOOK_CLASS(){}
    ~NCB_GET_INSTANCE_HOOK_CLASS(){}
    NCB_INSTANCE_GETTER(objthis) { 
        ClassT *obj = GetNativeInstance(objthis);
        if(!obj) {
            SetNativeInstance(objthis, (obj = new ClassT()));
        }
        return obj;
    }
};

NCB_ATTACH_CLASS_WITH_HOOK(MenuItemExt, MenuItem) {
    NCB_METHOD_RAW_CALLBACK(textToKeycode, MenuItemExt::textToKeycode, 0);
    NCB_METHOD_RAW_CALLBACK(keycodeToText, MenuItemExt::keycodeToText, 0);
    NCB_PROPERTY(HMENU, getHMENU, setHMENU);
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

    void setMenu(tTJSVariant v) {
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
