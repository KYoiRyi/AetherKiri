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
    static tTJSVariant textToKeycode(const tjs_char* text) {
        return tTJSVariant((tjs_int)0);
    }

    static tTJSVariant keycodeToText(tTJSVariant keycode) {
        return tTJSVariant(TJS_W(""));
    }

    tTJSVariant getHMENU() const {
        return tTJSVariant((tjs_int)0);
    }
};

NCB_ATTACH_CLASS(MenuItemExt, MenuItem) {
    NCB_METHOD(textToKeycode);
    NCB_METHOD(keycodeToText);
    NCB_PROPERTY_RO(HMENU, getHMENU);
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
