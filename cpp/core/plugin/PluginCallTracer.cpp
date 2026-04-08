/**
 * @file PluginCallTracer.cpp
 * @brief Implementation of the plugin call tracing system.
 */

#include "PluginCallTracer.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <cstring>
#include <sys/stat.h>

// ===========================================================================
// PluginCallTracer singleton
// ===========================================================================

PluginCallTracer &PluginCallTracer::Instance() {
    static PluginCallTracer instance;
    return instance;
}

void PluginCallTracer::InitLogger(const std::string &logFilePath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_loggerInitialized) return;
    m_logFilePath = logFilePath;

    try {
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
            logFilePath, /*truncate=*/true);
        m_logger = std::make_shared<spdlog::logger>("plugin_trace", sink);
        m_logger->set_pattern("[%H:%M:%S.%e] %v");
        m_logger->flush_on(spdlog::level::info);
        spdlog::register_logger(m_logger);
        m_loggerInitialized = true;
        m_logger->info("=== Plugin Call Trace Started ===");
    } catch (const std::exception &e) {
        spdlog::warn("PluginCallTracer: failed to create logger at '{}': {}",
                     logFilePath, e.what());
    }
}

void PluginCallTracer::SetEnabled(bool enabled) {
    m_enabled = enabled;
    if (m_logger) {
        m_logger->info("=== Plugin tracing {} ===", enabled ? "enabled" : "disabled");
        m_logger->flush();
    }
}

void PluginCallTracer::EnsureLogger() {
    if (!m_loggerInitialized && !m_logFilePath.empty()) {
        InitLogger(m_logFilePath);
    }
}

iTJSDispatch2 *PluginCallTracer::WrapDispatch(const ttstr &className,
                                               const ttstr &memberName,
                                               iTJSDispatch2 *original,
                                               tTJSNativeInstanceType type) {
    if (!original) return nullptr;
    if (!m_enabled) return original;

    std::string cn, mn;
    { // Convert ttstr to narrow strings for logging
        tTJSNarrowStringHolder nc(className.c_str());
        tTJSNarrowStringHolder nm(memberName.c_str());
        cn = nc.operator const char *();
        mn = nm.operator const char *();
    }

    if (type == nitProperty) {
        return new PluginPropertyProxy(cn, mn, original);
    } else {
        // nitMethod, nitClass, etc. — all go through FuncCall dispatch
        return new PluginMethodProxy(cn, mn, original);
    }
}

void PluginCallTracer::LogMethodCall(const std::string &className,
                                     const std::string &memberName,
                                     tjs_int numparams,
                                     tTJSVariant **param) {
    if (!m_logger) return;
    std::string msg = className + "." + memberName + "(argc=" +
                      std::to_string(numparams);

    // Append up to 4 argument representations
    const tjs_int maxArgs = numparams > 4 ? 4 : numparams;
    for (tjs_int i = 0; i < maxArgs; ++i) {
        if (param && param[i]) {
            try {
                ttstr s(*param[i]);
                tTJSNarrowStringHolder ns(s.c_str());
                std::string val = ns.operator const char *();
                // Truncate long values
                if (val.size() > 64) val.resize(64);
                msg += ", arg" + std::to_string(i) + "=" + val;
            } catch (...) {
                msg += ", arg" + std::to_string(i) + "=<?>";;
            }
        }
    }
    if (numparams > 4) msg += ", ...";
    msg += ")";

    m_logger->info(msg);
}

void PluginCallTracer::LogPropGet(const std::string &className,
                                  const std::string &memberName) {
    if (!m_logger) return;
    m_logger->info("{}.{} [GET]", className, memberName);
}

void PluginCallTracer::LogPropSet(const std::string &className,
                                  const std::string &memberName,
                                  const tTJSVariant *value) {
    if (!m_logger) return;
    std::string valStr;
    if (value) {
        try {
            ttstr s(*value);
            tTJSNarrowStringHolder ns(s.c_str());
            valStr = ns.operator const char *();
            if (valStr.size() > 64) valStr.resize(64);
        } catch (...) {
            valStr = "<?>";
        }
    } else {
        valStr = "(null)";
    }
    m_logger->info("{}.{} [SET] {}", className, memberName, valStr);
}

// ===========================================================================
// PluginMethodProxy
// ===========================================================================

PluginMethodProxy::PluginMethodProxy(const std::string &className,
                                     const std::string &memberName,
                                     iTJSDispatch2 *original)
    : m_className(className), m_memberName(memberName), m_original(original) {
    if (m_original) m_original->AddRef();
}

PluginMethodProxy::~PluginMethodProxy() {
    if (m_original) m_original->Release();
}

tjs_uint PluginMethodProxy::AddRef() { return tTJSDispatch::AddRef(); }
tjs_uint PluginMethodProxy::Release() { return tTJSDispatch::Release(); }

tjs_error PluginMethodProxy::FuncCall(tjs_uint32 flag,
                                      const tjs_char *membername,
                                      tjs_uint32 *hint, tTJSVariant *result,
                                      tjs_int numparams, tTJSVariant **param,
                                      iTJSDispatch2 *objthis) {
    if (!membername) {
        // Actual method invocation (membername==nullptr means resolved)
        PluginCallTracer::Instance().LogMethodCall(
            m_className, m_memberName, numparams, param);
    }
    return m_original->FuncCall(flag, membername, hint, result, numparams, param, objthis);
}

// --- Delegate everything else ---

tjs_error PluginMethodProxy::FuncCallByNum(tjs_uint32 flag, tjs_int num,
                                           tTJSVariant *result,
                                           tjs_int numparams,
                                           tTJSVariant **param,
                                           iTJSDispatch2 *objthis) {
    return m_original->FuncCallByNum(flag, num, result, numparams, param, objthis);
}

tjs_error PluginMethodProxy::PropGet(tjs_uint32 flag,
                                     const tjs_char *membername,
                                     tjs_uint32 *hint, tTJSVariant *result,
                                     iTJSDispatch2 *objthis) {
    return m_original->PropGet(flag, membername, hint, result, objthis);
}

tjs_error PluginMethodProxy::PropGetByNum(tjs_uint32 flag, tjs_int num,
                                          tTJSVariant *result,
                                          iTJSDispatch2 *objthis) {
    return m_original->PropGetByNum(flag, num, result, objthis);
}

tjs_error PluginMethodProxy::PropSet(tjs_uint32 flag,
                                     const tjs_char *membername,
                                     tjs_uint32 *hint, const tTJSVariant *param,
                                     iTJSDispatch2 *objthis) {
    return m_original->PropSet(flag, membername, hint, param, objthis);
}

tjs_error PluginMethodProxy::PropSetByNum(tjs_uint32 flag, tjs_int num,
                                          const tTJSVariant *param,
                                          iTJSDispatch2 *objthis) {
    return m_original->PropSetByNum(flag, num, param, objthis);
}

tjs_error PluginMethodProxy::GetCount(tjs_int *result,
                                      const tjs_char *membername,
                                      tjs_uint32 *hint,
                                      iTJSDispatch2 *objthis) {
    return m_original->GetCount(result, membername, hint, objthis);
}

tjs_error PluginMethodProxy::GetCountByNum(tjs_int *result, tjs_int num,
                                           iTJSDispatch2 *objthis) {
    return m_original->GetCountByNum(result, num, objthis);
}

tjs_error PluginMethodProxy::PropSetByVS(tjs_uint32 flag,
                                         tTJSVariantString *membername,
                                         const tTJSVariant *param,
                                         iTJSDispatch2 *objthis) {
    return m_original->PropSetByVS(flag, membername, param, objthis);
}

tjs_error PluginMethodProxy::EnumMembers(tjs_uint32 flag,
                                         tTJSVariantClosure *callback,
                                         iTJSDispatch2 *objthis) {
    return m_original->EnumMembers(flag, callback, objthis);
}

tjs_error PluginMethodProxy::DeleteMember(tjs_uint32 flag,
                                          const tjs_char *membername,
                                          tjs_uint32 *hint,
                                          iTJSDispatch2 *objthis) {
    return m_original->DeleteMember(flag, membername, hint, objthis);
}

tjs_error PluginMethodProxy::DeleteMemberByNum(tjs_uint32 flag, tjs_int num,
                                               iTJSDispatch2 *objthis) {
    return m_original->DeleteMemberByNum(flag, num, objthis);
}

tjs_error PluginMethodProxy::Invalidate(tjs_uint32 flag,
                                        const tjs_char *membername,
                                        tjs_uint32 *hint,
                                        iTJSDispatch2 *objthis) {
    return m_original->Invalidate(flag, membername, hint, objthis);
}

tjs_error PluginMethodProxy::InvalidateByNum(tjs_uint32 flag, tjs_int num,
                                             iTJSDispatch2 *objthis) {
    return m_original->InvalidateByNum(flag, num, objthis);
}

tjs_error PluginMethodProxy::IsValid(tjs_uint32 flag,
                                     const tjs_char *membername,
                                     tjs_uint32 *hint,
                                     iTJSDispatch2 *objthis) {
    return m_original->IsValid(flag, membername, hint, objthis);
}

tjs_error PluginMethodProxy::IsValidByNum(tjs_uint32 flag, tjs_int num,
                                          iTJSDispatch2 *objthis) {
    return m_original->IsValidByNum(flag, num, objthis);
}

tjs_error PluginMethodProxy::CreateNew(tjs_uint32 flag,
                                       const tjs_char *membername,
                                       tjs_uint32 *hint,
                                       iTJSDispatch2 **result,
                                       tjs_int numparams,
                                       tTJSVariant **param,
                                       iTJSDispatch2 *objthis) {
    return m_original->CreateNew(flag, membername, hint, result, numparams, param, objthis);
}

tjs_error PluginMethodProxy::CreateNewByNum(tjs_uint32 flag, tjs_int num,
                                            iTJSDispatch2 **result,
                                            tjs_int numparams,
                                            tTJSVariant **param,
                                            iTJSDispatch2 *objthis) {
    return m_original->CreateNewByNum(flag, num, result, numparams, param, objthis);
}

tjs_error PluginMethodProxy::Reserved1() {
    return m_original->Reserved1();
}

tjs_error PluginMethodProxy::IsInstanceOf(tjs_uint32 flag,
                                          const tjs_char *membername,
                                          tjs_uint32 *hint,
                                          const tjs_char *classname,
                                          iTJSDispatch2 *objthis) {
    return m_original->IsInstanceOf(flag, membername, hint, classname, objthis);
}

tjs_error PluginMethodProxy::IsInstanceOfByNum(tjs_uint32 flag, tjs_int num,
                                               const tjs_char *classname,
                                               iTJSDispatch2 *objthis) {
    return m_original->IsInstanceOfByNum(flag, num, classname, objthis);
}

tjs_error PluginMethodProxy::Operation(tjs_uint32 flag,
                                       const tjs_char *membername,
                                       tjs_uint32 *hint, tTJSVariant *result,
                                       const tTJSVariant *param,
                                       iTJSDispatch2 *objthis) {
    return m_original->Operation(flag, membername, hint, result, param, objthis);
}

tjs_error PluginMethodProxy::OperationByNum(tjs_uint32 flag, tjs_int num,
                                            tTJSVariant *result,
                                            const tTJSVariant *param,
                                            iTJSDispatch2 *objthis) {
    return m_original->OperationByNum(flag, num, result, param, objthis);
}

tjs_error PluginMethodProxy::NativeInstanceSupport(tjs_uint32 flag,
                                                   tjs_int32 classid,
                                                   iTJSNativeInstance **pointer) {
    return m_original->NativeInstanceSupport(flag, classid, pointer);
}

tjs_error PluginMethodProxy::ClassInstanceInfo(tjs_uint32 flag, tjs_uint num,
                                               tTJSVariant *value) {
    return m_original->ClassInstanceInfo(flag, num, value);
}

tjs_error PluginMethodProxy::Reserved2() { return m_original->Reserved2(); }
tjs_error PluginMethodProxy::Reserved3() { return m_original->Reserved3(); }

// ===========================================================================
// PluginPropertyProxy
// ===========================================================================

PluginPropertyProxy::PluginPropertyProxy(const std::string &className,
                                         const std::string &memberName,
                                         iTJSDispatch2 *original)
    : m_className(className), m_memberName(memberName), m_original(original) {
    if (m_original) m_original->AddRef();
}

PluginPropertyProxy::~PluginPropertyProxy() {
    if (m_original) m_original->Release();
}

tjs_uint PluginPropertyProxy::AddRef() { return tTJSDispatch::AddRef(); }
tjs_uint PluginPropertyProxy::Release() { return tTJSDispatch::Release(); }

tjs_error PluginPropertyProxy::PropGet(tjs_uint32 flag,
                                       const tjs_char *membername,
                                       tjs_uint32 *hint, tTJSVariant *result,
                                       iTJSDispatch2 *objthis) {
    if (!membername) {
        PluginCallTracer::Instance().LogPropGet(m_className, m_memberName);
    }
    return m_original->PropGet(flag, membername, hint, result, objthis);
}

tjs_error PluginPropertyProxy::PropSet(tjs_uint32 flag,
                                       const tjs_char *membername,
                                       tjs_uint32 *hint,
                                       const tTJSVariant *param,
                                       iTJSDispatch2 *objthis) {
    if (!membername) {
        PluginCallTracer::Instance().LogPropSet(m_className, m_memberName, param);
    }
    return m_original->PropSet(flag, membername, hint, param, objthis);
}

tjs_error PluginPropertyProxy::FuncCall(tjs_uint32 flag,
                                        const tjs_char *membername,
                                        tjs_uint32 *hint, tTJSVariant *result,
                                        tjs_int numparams, tTJSVariant **param,
                                        iTJSDispatch2 *objthis) {
    return m_original->FuncCall(flag, membername, hint, result, numparams, param, objthis);
}

// --- Delegate everything else ---

tjs_error PluginPropertyProxy::FuncCallByNum(tjs_uint32 flag, tjs_int num,
                                             tTJSVariant *result,
                                             tjs_int numparams,
                                             tTJSVariant **param,
                                             iTJSDispatch2 *objthis) {
    return m_original->FuncCallByNum(flag, num, result, numparams, param, objthis);
}

tjs_error PluginPropertyProxy::PropGetByNum(tjs_uint32 flag, tjs_int num,
                                            tTJSVariant *result,
                                            iTJSDispatch2 *objthis) {
    return m_original->PropGetByNum(flag, num, result, objthis);
}

tjs_error PluginPropertyProxy::PropSetByNum(tjs_uint32 flag, tjs_int num,
                                            const tTJSVariant *param,
                                            iTJSDispatch2 *objthis) {
    return m_original->PropSetByNum(flag, num, param, objthis);
}

tjs_error PluginPropertyProxy::GetCount(tjs_int *result,
                                        const tjs_char *membername,
                                        tjs_uint32 *hint,
                                        iTJSDispatch2 *objthis) {
    return m_original->GetCount(result, membername, hint, objthis);
}

tjs_error PluginPropertyProxy::GetCountByNum(tjs_int *result, tjs_int num,
                                             iTJSDispatch2 *objthis) {
    return m_original->GetCountByNum(result, num, objthis);
}

tjs_error PluginPropertyProxy::PropSetByVS(tjs_uint32 flag,
                                           tTJSVariantString *membername,
                                           const tTJSVariant *param,
                                           iTJSDispatch2 *objthis) {
    return m_original->PropSetByVS(flag, membername, param, objthis);
}

tjs_error PluginPropertyProxy::EnumMembers(tjs_uint32 flag,
                                           tTJSVariantClosure *callback,
                                           iTJSDispatch2 *objthis) {
    return m_original->EnumMembers(flag, callback, objthis);
}

tjs_error PluginPropertyProxy::DeleteMember(tjs_uint32 flag,
                                            const tjs_char *membername,
                                            tjs_uint32 *hint,
                                            iTJSDispatch2 *objthis) {
    return m_original->DeleteMember(flag, membername, hint, objthis);
}

tjs_error PluginPropertyProxy::DeleteMemberByNum(tjs_uint32 flag, tjs_int num,
                                                 iTJSDispatch2 *objthis) {
    return m_original->DeleteMemberByNum(flag, num, objthis);
}

tjs_error PluginPropertyProxy::Invalidate(tjs_uint32 flag,
                                          const tjs_char *membername,
                                          tjs_uint32 *hint,
                                          iTJSDispatch2 *objthis) {
    return m_original->Invalidate(flag, membername, hint, objthis);
}

tjs_error PluginPropertyProxy::InvalidateByNum(tjs_uint32 flag, tjs_int num,
                                               iTJSDispatch2 *objthis) {
    return m_original->InvalidateByNum(flag, num, objthis);
}

tjs_error PluginPropertyProxy::IsValid(tjs_uint32 flag,
                                       const tjs_char *membername,
                                       tjs_uint32 *hint,
                                       iTJSDispatch2 *objthis) {
    return m_original->IsValid(flag, membername, hint, objthis);
}

tjs_error PluginPropertyProxy::IsValidByNum(tjs_uint32 flag, tjs_int num,
                                            iTJSDispatch2 *objthis) {
    return m_original->IsValidByNum(flag, num, objthis);
}

tjs_error PluginPropertyProxy::CreateNew(tjs_uint32 flag,
                                         const tjs_char *membername,
                                         tjs_uint32 *hint,
                                         iTJSDispatch2 **result,
                                         tjs_int numparams,
                                         tTJSVariant **param,
                                         iTJSDispatch2 *objthis) {
    return m_original->CreateNew(flag, membername, hint, result, numparams, param, objthis);
}

tjs_error PluginPropertyProxy::CreateNewByNum(tjs_uint32 flag, tjs_int num,
                                              iTJSDispatch2 **result,
                                              tjs_int numparams,
                                              tTJSVariant **param,
                                              iTJSDispatch2 *objthis) {
    return m_original->CreateNewByNum(flag, num, result, numparams, param, objthis);
}

tjs_error PluginPropertyProxy::Reserved1() {
    return m_original->Reserved1();
}

tjs_error PluginPropertyProxy::IsInstanceOf(tjs_uint32 flag,
                                            const tjs_char *membername,
                                            tjs_uint32 *hint,
                                            const tjs_char *classname,
                                            iTJSDispatch2 *objthis) {
    return m_original->IsInstanceOf(flag, membername, hint, classname, objthis);
}

tjs_error PluginPropertyProxy::IsInstanceOfByNum(tjs_uint32 flag, tjs_int num,
                                                 const tjs_char *classname,
                                                 iTJSDispatch2 *objthis) {
    return m_original->IsInstanceOfByNum(flag, num, classname, objthis);
}

tjs_error PluginPropertyProxy::Operation(tjs_uint32 flag,
                                         const tjs_char *membername,
                                         tjs_uint32 *hint, tTJSVariant *result,
                                         const tTJSVariant *param,
                                         iTJSDispatch2 *objthis) {
    return m_original->Operation(flag, membername, hint, result, param, objthis);
}

tjs_error PluginPropertyProxy::OperationByNum(tjs_uint32 flag, tjs_int num,
                                              tTJSVariant *result,
                                              const tTJSVariant *param,
                                              iTJSDispatch2 *objthis) {
    return m_original->OperationByNum(flag, num, result, param, objthis);
}

tjs_error PluginPropertyProxy::NativeInstanceSupport(tjs_uint32 flag,
                                                     tjs_int32 classid,
                                                     iTJSNativeInstance **pointer) {
    return m_original->NativeInstanceSupport(flag, classid, pointer);
}

tjs_error PluginPropertyProxy::ClassInstanceInfo(tjs_uint32 flag, tjs_uint num,
                                                 tTJSVariant *value) {
    return m_original->ClassInstanceInfo(flag, num, value);
}

tjs_error PluginPropertyProxy::Reserved2() { return m_original->Reserved2(); }
tjs_error PluginPropertyProxy::Reserved3() { return m_original->Reserved3(); }

// ===========================================================================
// Registration phase logging
// ===========================================================================

static const char *TypeToStr(tTJSNativeInstanceType type) {
    switch (type) {
    case nitMethod:   return "method";
    case nitProperty: return "property";
    case nitClass:    return "class";
    default:          return "unknown";
    }
}

void PluginCallTracer::LogRegistrationStart() {
    if (!m_logger) return;
    m_logger->info("");
    m_logger->info("====== Plugin Registration ======");
}

void PluginCallTracer::LogRegistration(const ttstr &className,
                                       const ttstr &memberName,
                                       tTJSNativeInstanceType type,
                                       tjs_uint32 flags) {
    if (!m_logger) return;
    tTJSNarrowStringHolder nc(className.c_str());
    tTJSNarrowStringHolder nm(memberName.c_str());
    std::string cn = nc.operator const char *();
    std::string mn = nm.operator const char *();
    const char *ts = TypeToStr(type);
    bool isStatic = (flags & TJS_STATICMEMBER) != 0;

    m_logger->info("  [{}] {}.{}{}", ts, cn, mn, isStatic ? " (static)" : "");
}

void PluginCallTracer::LogRegistrationEnd() {
    if (!m_logger) return;
    m_logger->info("====== Registration Complete ======");
    m_logger->info("");
    m_logger->flush();
}

void PluginCallTracer::LogPluginLoad(const std::string &name, bool success,
                                     const char *stub) {
    if (!m_logger) return;
    if (success) {
        m_logger->info("[Plugin] {} loaded OK", name);
    } else if (stub) {
        m_logger->info("[Plugin] {} MISSING → fallback: {}", name, stub);
    } else {
        m_logger->info("[Plugin] {} MISSING (no fallback)", name);
    }
    m_logger->flush();
}
