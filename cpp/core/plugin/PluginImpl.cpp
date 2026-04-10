//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// "Plugins" class implementation / Service for plug-ins
//---------------------------------------------------------------------------
#include <set>
#include <algorithm>
#include <functional>

#include <spdlog/spdlog.h>

#include "tjsCommHead.h"

#include "ScriptMgnIntf.h"
#include "PluginImpl.h"

#include "StorageImpl.h"

#include "EventIntf.h"
#include "TransIntf.h"
#include "tjsArray.h"
#include "DebugIntf.h"

#include "tjs.h"
#include "tjsConfig.h"
#include "ncbind.hpp"
#include "combase.h"

#ifdef TVP_SUPPORT_KPI
#include "kmp_pi.h"
#endif

#include "FilePathUtil.h"
#include "Application.h"
#include "SysInitImpl.h"

#ifdef _MSC_VER
#define strcasecmp _stricmp
#endif

//---------------------------------------------------------------------------
bool TVPLoadInternalPlugin(const ttstr &_name);
bool TVPRegisterGlobalObject(const tjs_char *name, iTJSDispatch2 *dsp);

static iTJSDispatch2 *s_ProxyStorageMap = nullptr;
static iTVPStorageMedia *s_ProxyStorageMedia = nullptr;

class tTJSNI_GamepadStub : public tTJSNativeInstance {
public:
    tjs_error Construct(tjs_int, tTJSVariant **, iTJSDispatch2 *) override {
        return TJS_S_OK;
    }

    void Invalidate() override {}
};

class tTJSNI_gfxFireStub : public tTJSNativeInstance {
public:
    tjs_error Construct(tjs_int, tTJSVariant **, iTJSDispatch2 *) override {
        return TJS_S_OK;
    }
    void Invalidate() override {}
};

class tTJSNC_GamepadStub : public tTJSNativeClass {
public:
    static tjs_uint32 ClassID;

    tTJSNC_GamepadStub() : tTJSNativeClass(TJS_W("GamepadPort")) {
        TJS_BEGIN_NATIVE_MEMBERS(GamepadPort)
        TJS_DECL_EMPTY_FINALIZE_METHOD

        TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL(
            _this, tTJSNI_GamepadStub, GamepadPort) {
            return TJS_S_OK;
        }
        TJS_END_NATIVE_CONSTRUCTOR_DECL(GamepadPort)

        TJS_BEGIN_NATIVE_METHOD_DECL(update) { return TJS_S_OK; }
        TJS_END_NATIVE_METHOD_DECL(update)

        TJS_BEGIN_NATIVE_METHOD_DECL(initialize) {
            if(result)
                *result = 1;
            return TJS_S_OK;
        }
        TJS_END_NATIVE_METHOD_DECL(initialize)

        TJS_BEGIN_NATIVE_METHOD_DECL(refresh) { return TJS_S_OK; }
        TJS_END_NATIVE_METHOD_DECL(refresh)

        TJS_BEGIN_NATIVE_METHOD_DECL(remove) { return TJS_S_OK; }
        TJS_END_NATIVE_METHOD_DECL(remove)

        TJS_BEGIN_NATIVE_METHOD_DECL(rebind) { return TJS_S_OK; }
        TJS_END_NATIVE_METHOD_DECL(rebind)

        TJS_BEGIN_NATIVE_METHOD_DECL(reDetect) { return TJS_S_OK; }
        TJS_END_NATIVE_METHOD_DECL(reDetect)

        TJS_BEGIN_NATIVE_METHOD_DECL(createInputDevice) {
            if(result)
                *result = 0;
            return TJS_S_OK;
        }
        TJS_END_NATIVE_METHOD_DECL(createInputDevice)

        TJS_BEGIN_NATIVE_METHOD_DECL(getPadKeyState) {
            if(result)
                *result = 0;
            return TJS_S_OK;
        }
        TJS_END_NATIVE_METHOD_DECL(getPadKeyState)

        TJS_BEGIN_NATIVE_METHOD_DECL(getButtonState) {
            if(result)
                *result = 0;
            return TJS_S_OK;
        }
        TJS_END_NATIVE_METHOD_DECL(getButtonState)

        TJS_BEGIN_NATIVE_METHOD_DECL(getAxisState) {
            if(result)
                *result = 0.0;
            return TJS_S_OK;
        }
        TJS_END_NATIVE_METHOD_DECL(getAxisState)

        TJS_BEGIN_NATIVE_METHOD_DECL(getCount) {
            if(result)
                *result = 0;
            return TJS_S_OK;
        }
        TJS_END_NATIVE_METHOD_DECL(getCount)

        TJS_BEGIN_NATIVE_PROP_DECL(count) {
            TJS_BEGIN_NATIVE_PROP_GETTER {
                *result = 0;
                return TJS_S_OK;
            }
            TJS_END_NATIVE_PROP_GETTER
            TJS_DENY_NATIVE_PROP_SETTER
        }
        TJS_END_NATIVE_PROP_DECL(count)

        TJS_BEGIN_NATIVE_PROP_DECL(port) {
            TJS_BEGIN_NATIVE_PROP_GETTER {
                *result = 0;
                return TJS_S_OK;
            }
            TJS_END_NATIVE_PROP_GETTER
            TJS_BEGIN_NATIVE_PROP_SETTER { return TJS_S_OK; }
            TJS_END_NATIVE_PROP_SETTER
        }
        TJS_END_NATIVE_PROP_DECL(port)

        TJS_BEGIN_NATIVE_PROP_DECL(portIndex) {
            TJS_BEGIN_NATIVE_PROP_GETTER {
                *result = 0;
                return TJS_S_OK;
            }
            TJS_END_NATIVE_PROP_GETTER
            TJS_BEGIN_NATIVE_PROP_SETTER { return TJS_S_OK; }
            TJS_END_NATIVE_PROP_SETTER
        }
        TJS_END_NATIVE_PROP_DECL(portIndex)

        TJS_BEGIN_NATIVE_PROP_DECL(enabled) {
            TJS_BEGIN_NATIVE_PROP_GETTER {
                *result = 0;
                return TJS_S_OK;
            }
            TJS_END_NATIVE_PROP_GETTER
            TJS_BEGIN_NATIVE_PROP_SETTER { return TJS_S_OK; }
            TJS_END_NATIVE_PROP_SETTER
        }
        TJS_END_NATIVE_PROP_DECL(enabled)

        TJS_BEGIN_NATIVE_PROP_DECL(connected) {
            TJS_BEGIN_NATIVE_PROP_GETTER {
                *result = 0;
                return TJS_S_OK;
            }
            TJS_END_NATIVE_PROP_GETTER
            TJS_DENY_NATIVE_PROP_SETTER
        }
        TJS_END_NATIVE_PROP_DECL(connected)

        TJS_BEGIN_NATIVE_PROP_DECL(name) {
            TJS_BEGIN_NATIVE_PROP_GETTER {
                *result = TJS_W("");
                return TJS_S_OK;
            }
            TJS_END_NATIVE_PROP_GETTER
            TJS_DENY_NATIVE_PROP_SETTER
        }
        TJS_END_NATIVE_PROP_DECL(name)

        TJS_BEGIN_NATIVE_PROP_DECL(HWND) {
            TJS_BEGIN_NATIVE_PROP_GETTER {
                *result = static_cast<tjs_int64>(0);
                return TJS_S_OK;
            }
            TJS_END_NATIVE_PROP_GETTER
            TJS_BEGIN_NATIVE_PROP_SETTER { return TJS_S_OK; }
            TJS_END_NATIVE_PROP_SETTER
        }
        TJS_END_NATIVE_PROP_DECL(HWND)

        TJS_END_NATIVE_MEMBERS
    }

protected:
    tTJSNativeInstance *CreateNativeInstance() override {
        return new tTJSNI_GamepadStub();
    }
};

class tTJSNC_gfxFireStub : public tTJSNativeClass {
public:
    static tjs_uint32 ClassID;

    tTJSNC_gfxFireStub() : tTJSNativeClass(TJS_W("gfxFire")) {
        TJS_BEGIN_NATIVE_MEMBERS(gfxFire)
        TJS_DECL_EMPTY_FINALIZE_METHOD
        TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL(_this, tTJSNI_gfxFireStub, gfxFire) {
            return TJS_S_OK;
        }
        TJS_END_NATIVE_CONSTRUCTOR_DECL(gfxFire)
        TJS_END_NATIVE_MEMBERS
    }

protected:
    tTJSNativeInstance *CreateNativeInstance() override {
        return new tTJSNI_gfxFireStub();
    }
};

tjs_uint32 tTJSNC_GamepadStub::ClassID = static_cast<tjs_uint32>(-1);
tjs_uint32 tTJSNC_gfxFireStub::ClassID = static_cast<tjs_uint32>(-1);

static ttstr TVPGetNormalizedPluginName(const ttstr &name) {
    ttstr shortName = TVPExtractStorageName(name);
    shortName.ToLowerCase();
    return shortName;
}

static bool TVPIsProxyPluginName(const ttstr &name) {
    return name == TJS_W("proxyfs.dll") || name == TJS_W("yuzuex.dll");
}

static bool TVPHasRegisteredProxyPluginAlias() {
    return TVPRegisteredPlugins.find(TJS_W("proxyfs.dll")) !=
               TVPRegisteredPlugins.end() ||
           TVPRegisteredPlugins.find(TJS_W("yuzuex.dll")) !=
               TVPRegisteredPlugins.end();
}

static bool TVPQueryProxyValue(const ttstr &name, tTJSVariant &value) {
    if(!s_ProxyStorageMap)
        return false;

    ttstr key = TVPExtractStorageName(name);
    if(key.IsEmpty())
        return false;

    key.ToLowerCase();

    return TJS_SUCCEEDED(s_ProxyStorageMap->PropGet(
        TJS_MEMBERMUSTEXIST, key.c_str(), nullptr, &value, s_ProxyStorageMap));
}

static bool TVPLookupProxyTarget(const ttstr &name, ttstr *resolved) {
    tTJSVariant value;
    if(!TVPQueryProxyValue(name, value) || value.Type() != tvtString)
        return false;

    if(resolved) {
        *resolved = value.GetString();
        return !resolved->IsEmpty();
    }

    return true;
}

class tTVPProxyObjectLister : public tTJSDispatch {
    iTVPStorageLister *Lister;
    ttstr RequestedPath;

public:
    tTVPProxyObjectLister(const ttstr &requestedPath, iTVPStorageLister *lister) :
        Lister(lister), RequestedPath(requestedPath) {}

    tjs_error FuncCall(tjs_uint32, const tjs_char *, tjs_uint32 *,
                       tTJSVariant *result, tjs_int numparams,
                       tTJSVariant **param, iTJSDispatch2 *) override {
        if(numparams > 1 && !(param[1]->AsInteger() & TJS_HIDDENMEMBER)) {
            ttstr memberName = param[0]->GetString();
            if(TVPExtractStoragePath(memberName) == RequestedPath)
                Lister->Add(TVPExtractStorageName(memberName));
        }

        if(result)
            *result = true;

        return TJS_S_OK;
    }
};

struct tTVPProxyLocalNameTryData {
    ttstr *Name;
};

static void TJS_USERENTRY TVPProxyLocalNameTry(void *data) {
    auto *arg = static_cast<tTVPProxyLocalNameTryData *>(data);
    TVPGetLocalName(*arg->Name);
}

static bool TJS_USERENTRY TVPProxyLocalNameCatch(void *data,
                                                 const tTVPExceptionDesc &) {
    auto *arg = static_cast<tTVPProxyLocalNameTryData *>(data);
    arg->Name->Clear();
    return false;
}

class tTVPProxyStorageMedia : public iTVPStorageMedia {
    tjs_uint RefCount = 1;
public:
    void AddRef() override { RefCount++; }
    void Release() override { if(RefCount == 1) delete this; else RefCount--; }
    void GetName(ttstr &name) override { name = TJS_W("proxy"); }
    void NormalizeDomainName(ttstr &) override {}
    void NormalizePathName(ttstr &) override {}
    bool CheckExistentStorage(const ttstr &name) override {
        return TVPLookupProxyTarget(name, nullptr);
    }
    tTJSBinaryStream *Open(const ttstr &name, tjs_uint32 flags) override {
        ttstr resolved;
        if(!TVPLookupProxyTarget(name, &resolved)) {
            TVPThrowExceptionMessage(TJS_W("cannot open proxyfile:%1"), name);
            return nullptr;
        }

        IStream *stream = nullptr;
        tTJSBinaryStream *adapter = nullptr;

        try {
            stream = TVPCreateIStream(resolved, flags);
            if(stream)
                adapter = TVPCreateBinaryStreamAdapter(stream);
        } catch(...) {
            if(stream)
                stream->Release();
            TVPThrowExceptionMessage(TJS_W("cannot open proxyfile:%1"), name);
            return nullptr;
        }

        if(stream)
            stream->Release();

        if(adapter)
            return adapter;

        TVPThrowExceptionMessage(TJS_W("cannot open proxyfile:%1"), name);
        return nullptr;
    }
    void GetListAt(const ttstr &name, iTVPStorageLister *lister) override {
        if(!s_ProxyStorageMap)
            return;

        tTJSVariantClosure closure(new tTVPProxyObjectLister(name, lister));
        s_ProxyStorageMap->EnumMembers(TJS_IGNOREPROP, &closure,
                                       s_ProxyStorageMap);
        closure.Release();
    }
    void GetLocallyAccessibleName(ttstr &name) override {
        ttstr resolved;
        if(!TVPLookupProxyTarget(name, &resolved)) {
            name.Clear();
            return;
        }

        name = resolved;
        tTVPProxyLocalNameTryData data{ &name };
        TVPDoTryBlock(TVPProxyLocalNameTry, TVPProxyLocalNameCatch, nullptr,
                      &data);
    }
};

static bool TVPRegisterProxyFsStub() {
    if(s_ProxyStorageMap && s_ProxyStorageMedia)
        return true;

    iTJSDispatch2 *dict = TJSCreateDictionaryObject();
    if(!dict)
        return false;

    if(!TVPRegisterGlobalObject(TJS_W("ProxyStorageMap"), dict)) {
        dict->Release();
        return false;
    }

    s_ProxyStorageMap = dict;
    s_ProxyStorageMap->AddRef();
    dict->Release();

    s_ProxyStorageMedia = new tTVPProxyStorageMedia();
    TVPRegisterStorageMedia(s_ProxyStorageMedia);

    spdlog::info("Registered proxy storage media compatibility layer");
    return true;
}

static void TVPUnregisterProxyFsStub() {
    if(s_ProxyStorageMedia) {
        TVPUnregisterStorageMedia(s_ProxyStorageMedia);
        s_ProxyStorageMedia->Release();
        s_ProxyStorageMedia = nullptr;
    }

    if(s_ProxyStorageMap) {
        TVPRemoveGlobalObject(TJS_W("ProxyStorageMap"));
        s_ProxyStorageMap->Release();
        s_ProxyStorageMap = nullptr;
    }
}

// gamepad.dll 未实现时注册 stub，避免 exgamepad.tjs 访问 GamepadPort 报错（逆向见：global["GamepadPort"]/["Gamepad"]，脚本用 SystemConfig.GamepadPort）
static void TVPRegisterGamepadStub() {
    iTJSDispatch2 *stub = new tTJSNC_GamepadStub();
    if(!stub)
        return;

    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(!global) {
        stub->Release();
        return;
    }

    tTJSVariant val(stub);
    global->PropSet(TJS_MEMBERENSURE, TJS_W("GamepadPort"), nullptr, &val, global);
    global->PropSet(TJS_MEMBERENSURE, TJS_W("Gamepad"), nullptr, &val, global);

    tTJSVariant configVar;
    if(global->PropGet(0, TJS_W("SystemConfig"), nullptr, &configVar, global) == TJS_S_OK &&
       configVar.Type() == tvtObject && configVar.AsObjectNoAddRef()) {
        configVar.AsObjectNoAddRef()->PropSet(TJS_MEMBERENSURE, TJS_W("GamepadPort"), nullptr, &val,
                                              configVar.AsObjectNoAddRef());
    }

    global->Release();
    stub->Release();
    spdlog::info("Registered GamepadPort/Gamepad stub for missing gamepad.dll");
}

static void TVPRegisterGfxFireStub() {
    iTJSDispatch2 *stub = new tTJSNC_gfxFireStub();
    if(!stub)
        return;

    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(!global) {
        stub->Release();
        return;
    }

    tTJSVariant val(stub);
    global->PropSet(TJS_MEMBERENSURE, TJS_W("gfxFire"), nullptr, &val, global);

    global->Release();
    stub->Release();
    spdlog::info("Registered gfxFire stub for missing gfxEffect.dll");
}

void TVPLoadPlugin(const ttstr &name) {
    ttstr normalizedShortName = TVPGetNormalizedPluginName(name);

    auto pluginName = name;
    if(normalizedShortName == TJS_W("emoteplayer.dll"))
        pluginName = "motionplayer.dll";

    const char *stub = nullptr;
    bool loaded = TVPLoadInternalPlugin(pluginName);
    if(!loaded && TVPIsProxyPluginName(normalizedShortName)) {
        loaded = TVPRegisterProxyFsStub();
        if(loaded) {
            TVPRegisteredPlugins.insert(normalizedShortName);
            stub = "ProxyStorageMap compatibility layer";
        }
    }

    if(loaded) {
        spdlog::debug("Loading Plugin: {} Success", name.AsStdString());
        PluginCallTracer::Instance().LogPluginLoad(name.AsStdString(), true,
                                                   stub);
    } else {
        spdlog::error("Loading Plugin: {} Failed", name.AsStdString());
        const char *stub = nullptr;
        if(TJS::TVPIsMockEnabled()) {
            if(normalizedShortName == TJS_W("gamepad.dll")) {
                TVPRegisterGamepadStub();
                stub = "GamepadStub";
            } else if(normalizedShortName == TJS_W("gfxeffect.dll") ||
                      normalizedShortName == TJS_W("gfxfire.dll")) {
                TVPRegisterGfxFireStub();
                stub = "gfxFireStub";
            }
        }
        PluginCallTracer::Instance().LogPluginLoad(name.AsStdString(), false, stub);
    }
}

//---------------------------------------------------------------------------
bool TVPUnloadPlugin(const ttstr &name) {
    ttstr normalizedShortName = TVPGetNormalizedPluginName(name);
    if(TVPIsProxyPluginName(normalizedShortName)) {
        TVPRegisteredPlugins.erase(normalizedShortName);
        if(!TVPHasRegisteredProxyPluginAlias())
            TVPUnregisterProxyFsStub();
        return true;
    }

    // unload plugin
    return true;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// plug-in autoload support
//---------------------------------------------------------------------------
struct tTVPFoundPlugin {
    std::string Path;
    std::string Name;

    bool operator<(const tTVPFoundPlugin &rhs) const { return Name < rhs.Name; }
};

static tjs_int TVPAutoLoadPluginCount = 0;

static void TVPSearchPluginsAt(std::vector<tTVPFoundPlugin> &list,
                               std::string folder) {
    TVPListDir(folder, [&](const std::string &filename, int mask) {
        if(mask & S_IFREG) {
            if(filename.length() >= 4) {
                const char *ext = filename.c_str() + filename.length() - 4;
                if(!strcasecmp(ext, ".tpm") || !strcasecmp(ext, ".dll")) {
                    tTVPFoundPlugin fp;
                    fp.Path = folder;
                    fp.Name = filename;
                    list.emplace_back(fp);
                }
            }
        }
    });
}

void TVPLoadInternalPlugins() {
    PluginCallTracer::Instance().LogRegistrationStart();
    ncbAutoRegister::AllRegist();
    ncbAutoRegister::LoadAllModules();
    PluginCallTracer::Instance().LogRegistrationEnd();
}

bool TVPLoadInternalPlugin(const ttstr &_name) {
    /* 1. 拿到 ttstr 的原始缓冲区 */
    const tjs_char *src = _name.c_str();
    size_t len = _name.length();

    /* 2. 在 src 里找最后一个 '/' 或 '\\'，定位纯文件名起始 */
    const tjs_char *fileBegin = src;
    for(const tjs_char *p = src; *p; ++p) {
        if(*p == TJS_W('/') || *p == TJS_W('\\'))
            fileBegin = p + 1;
    }

    /* 3. 在 fileBegin 里找最后一个 '.' */
    const tjs_char *dot = nullptr;
    for(const tjs_char *p = fileBegin; *p; ++p) {
        if(*p == TJS_W('.'))
            dot = p; // 记录最后一个 '.'
    }

    /* 4. 检查后缀 .tpm（不区分大小写） */
    bool needReplace = false;
    if(dot && dot[1] && dot[2] && dot[3] && !dot[4]) // 长度正好 4：".tpm"
    {
        tjs_char low[5]; // 存放小写副本
        for(int i = 0; i < 4; ++i)
            low[i] = (tjs_char)towlower(dot[i]);
        low[4] = 0;

        if(TJS_strncmp(low, TJS_W(".tpm"), 4) == 0)
            needReplace = true;
    }

    /* 5. 构造结果字符串 */
    if(needReplace) {
        /* 需要替换为 .dll，计算新长度 */
        size_t newLen = len - 3 + 3; // 去掉 "tpm" 加上 "dll"
        tjs_char *buf = new tjs_char[newLen + 1];

        /* 拷贝前缀（含 .） */
        TJS_strncpy(buf, src, dot - src + 1);
        buf[dot - src + 1] = 0;

        /* 追加 dll */
        TJS_strcat(buf, TJS_W("dll"));

        ttstr fixed(buf);
        delete[] buf;

        return ncbAutoRegister::LoadModule(TVPExtractStorageName(fixed));
    }
    return ncbAutoRegister::LoadModule(TVPExtractStorageName(_name));
}

void tvpLoadPlugins() {
    TVPLoadInternalPlugins();
    // This function searches plugins which have an extension of
    // ".tpm" in the default path:
    //    1. a folder which holds kirikiri executable
    //    2. "plugin" folder of it
    // Plugin load order is to be decided using its name;
    // aaa.tpm is to be loaded before aab.tpm (sorted by ASCII order)

    // search plugins from path: (exepath), (exepath)\system,
    // (exepath)\plugin
    std::vector<tTVPFoundPlugin> list;

    std::string exepath = ExtractFileDir(TVPNativeProjectDir.AsStdString());

    TVPSearchPluginsAt(list, exepath);
    TVPSearchPluginsAt(list, exepath + "/system");
    TVPSearchPluginsAt(list, exepath + "/plugin");

    // sort by filename
    std::sort(list.begin(), list.end());

    // load each plugin
    TVPAutoLoadPluginCount = (tjs_int)list.size();
    for(auto &i : list) {
        TVPAddImportantLog(ttstr(TJS_W("(info) Loading ")) +
                           ttstr(i.Name.c_str()));
        TVPLoadPlugin((i.Path + "/" + i.Name).c_str());
    }
}

//---------------------------------------------------------------------------
tjs_int TVPGetAutoLoadPluginCount() { return TVPAutoLoadPluginCount; }

//---------------------------------------------------------------------------
// some service functions for plugin
//---------------------------------------------------------------------------
#include <zlib.h>

int ZLIB_uncompress(unsigned char *dest, unsigned long *destlen,
                    const unsigned char *source, unsigned long sourcelen) {
    return uncompress(dest, destlen, source, sourcelen);
}

//---------------------------------------------------------------------------
int ZLIB_compress(unsigned char *dest, unsigned long *destlen,
                  const unsigned char *source, unsigned long sourcelen) {
    return compress(dest, destlen, source, sourcelen);
}

//---------------------------------------------------------------------------
int ZLIB_compress2(unsigned char *dest, unsigned long *destlen,
                   const unsigned char *source, unsigned long sourcelen,
                   int level) {
    return compress2(dest, destlen, source, sourcelen, level);
}
//---------------------------------------------------------------------------
#include "md5.h"

static char TVP_assert_md5_state_t_size[(sizeof(TVP_md5_state_t) >=
                                         sizeof(md5_state_t))];

// if this errors, sizeof(TVP_md5_state_t) is not equal to
// sizeof(md5_state_t). sizeof(TVP_md5_state_t) must be equal to
// sizeof(md5_state_t).
//---------------------------------------------------------------------------
void TVP_md5_init(TVP_md5_state_t *pms) { md5_init((md5_state_t *)pms); }

//---------------------------------------------------------------------------
void TVP_md5_append(TVP_md5_state_t *pms, const tjs_uint8 *data, int nbytes) {
    md5_append((md5_state_t *)pms, (const md5_byte_t *)data, nbytes);
}

//---------------------------------------------------------------------------
void TVP_md5_finish(TVP_md5_state_t *pms, tjs_uint8 *digest) {
    md5_finish((md5_state_t *)pms, digest);
}

//---------------------------------------------------------------------------
bool TVPRegisterGlobalObject(const tjs_char *name, iTJSDispatch2 *dsp) {
    // register given object to global object
    tTJSVariant val(dsp);
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    tjs_error er;
    try {
        er = global->PropSet(TJS_MEMBERENSURE, name, nullptr, &val, global);
    } catch(...) {
        global->Release();
        return false;
    }
    global->Release();
    return TJS_SUCCEEDED(er);
}

//---------------------------------------------------------------------------
bool TVPRemoveGlobalObject(const tjs_char *name) {
    // remove registration of global object
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(!global)
        return false;
    tjs_error er;
    try {
        er = global->DeleteMember(0, name, nullptr, global);
    } catch(...) {
        global->Release();
        return false;
    }
    global->Release();
    return TJS_SUCCEEDED(er);
}

//---------------------------------------------------------------------------
void TVPDoTryBlock(tTVPTryBlockFunction tryblock,
                   tTVPCatchBlockFunction catchblock,
                   tTVPFinallyBlockFunction finallyblock, void *data) {
    try {
        tryblock(data);
    } catch(const eTJS &e) {
        if(finallyblock)
            finallyblock(data);
        tTVPExceptionDesc desc;
        desc.type = TJS_W("eTJS");
        desc.message = e.GetMessage();
        if(catchblock(data, desc))
            throw;
        return;
    } catch(...) {
        if(finallyblock)
            finallyblock(data);
        tTVPExceptionDesc desc;
        desc.type = TJS_W("unknown");
        if(catchblock(data, desc))
            throw;
        return;
    }
    if(finallyblock)
        finallyblock(data);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPCreateNativeClass_Plugins
//---------------------------------------------------------------------------
tTJSNativeClass *TVPCreateNativeClass_Plugins() {
    auto *cls = new tTJSNC_Plugins();

    // setup some platform-specific members
    //---------------------------------------------------------------------------

    //-- methods

    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ link) {

        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        ttstr name = *param[0];

        TVPLoadPlugin(name);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL_OUTER(
        /*object to register*/ cls,
        /*func. name*/ link)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ unlink) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        ttstr name = *param[0];

        bool res = TVPUnloadPlugin(name);

        if(result)
            *result = (tjs_int)res;

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL_OUTER(
        /*object to register*/ cls,
        /*func. name*/ unlink)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(getList) {
        iTJSDispatch2 *array = TJSCreateArrayObject();
        try {
            tjs_int idx = 0;
            for(const ttstr &name : TVPRegisteredPlugins) {
                tTJSVariant val(name);
                array->PropSetByNum(TJS_MEMBERENSURE, idx++, &val, array);
            }
            if(result)
                *result = tTJSVariant(array, array);
        } catch(...) {
            array->Release();
            throw;
        }
        array->Release();
        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL_OUTER(cls, getList)
    //---------------------------------------------------------------------------

    //---------------------------------------------------------------------------
    return cls;
}
//---------------------------------------------------------------------------
