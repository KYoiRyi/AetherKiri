/**
 * @file PluginCallTracer.hpp
 * @brief Traces all TJS2 → native plugin calls and writes them to plugin_trace.log.
 *
 * Interception happens at the ncbind registration level: each method/property
 * dispatch object registered by NCB_REGISTER_CLASS is wrapped with a thin proxy
 * that logs the call and then delegates to the original dispatch.
 *
 * Engine built-in classes (Layer, Window, Timer, …) are NOT traced because
 * they register through a different code path (TVPInitScriptEngine →
 * TVPCreateNativeClass_Xxx) that never touches ncbRegistNativeClass.
 */

#ifndef AETHERKiri_PLUGIN_CALL_TRACER_HPP
#define AETHERKiri_PLUGIN_CALL_TRACER_HPP

#include "tjsCommHead.h"
#include "tjsNative.h"
#include <spdlog/logger.h>
#include <mutex>
#include <string>

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
class PluginCallTracer;

// ---------------------------------------------------------------------------
// PluginMethodProxy — wraps a method dispatch, logs FuncCall then delegates
// ---------------------------------------------------------------------------
class PluginMethodProxy : public tTJSDispatch {
public:
    PluginMethodProxy(const std::string &className, const std::string &memberName,
                      iTJSDispatch2 *original);
    ~PluginMethodProxy() override;

    tjs_uint AddRef() override;
    tjs_uint Release() override;

    tjs_error FuncCall(tjs_uint32 flag, const tjs_char *membername,
                       tjs_uint32 *hint, tTJSVariant *result,
                       tjs_int numparams, tTJSVariant **param,
                       iTJSDispatch2 *objthis) override;

    // Delegate all other dispatch methods to original
    tjs_error FuncCallByNum(tjs_uint32 flag, tjs_int num, tTJSVariant *result,
                            tjs_int numparams, tTJSVariant **param,
                            iTJSDispatch2 *objthis) override;

    tjs_error PropGet(tjs_uint32 flag, const tjs_char *membername,
                      tjs_uint32 *hint, tTJSVariant *result,
                      iTJSDispatch2 *objthis) override;

    tjs_error PropGetByNum(tjs_uint32 flag, tjs_int num, tTJSVariant *result,
                           iTJSDispatch2 *objthis) override;

    tjs_error PropSet(tjs_uint32 flag, const tjs_char *membername,
                      tjs_uint32 *hint, const tTJSVariant *param,
                      iTJSDispatch2 *objthis) override;

    tjs_error PropSetByNum(tjs_uint32 flag, tjs_int num,
                           const tTJSVariant *param,
                           iTJSDispatch2 *objthis) override;

    tjs_error GetCount(tjs_int *result, const tjs_char *membername,
                       tjs_uint32 *hint, iTJSDispatch2 *objthis) override;

    tjs_error GetCountByNum(tjs_int *result, tjs_int num,
                            iTJSDispatch2 *objthis) override;

    tjs_error PropSetByVS(tjs_uint32 flag, tTJSVariantString *membername,
                          const tTJSVariant *param,
                          iTJSDispatch2 *objthis) override;

    tjs_error EnumMembers(tjs_uint32 flag, tTJSVariantClosure *callback,
                          iTJSDispatch2 *objthis) override;

    tjs_error DeleteMember(tjs_uint32 flag, const tjs_char *membername,
                           tjs_uint32 *hint, iTJSDispatch2 *objthis) override;

    tjs_error DeleteMemberByNum(tjs_uint32 flag, tjs_int num,
                                iTJSDispatch2 *objthis) override;

    tjs_error Invalidate(tjs_uint32 flag, const tjs_char *membername,
                         tjs_uint32 *hint, iTJSDispatch2 *objthis) override;

    tjs_error InvalidateByNum(tjs_uint32 flag, tjs_int num,
                              iTJSDispatch2 *objthis) override;

    tjs_error IsValid(tjs_uint32 flag, const tjs_char *membername,
                      tjs_uint32 *hint, iTJSDispatch2 *objthis) override;

    tjs_error IsValidByNum(tjs_uint32 flag, tjs_int num,
                           iTJSDispatch2 *objthis) override;

    tjs_error CreateNew(tjs_uint32 flag, const tjs_char *membername,
                        tjs_uint32 *hint, iTJSDispatch2 **result,
                        tjs_int numparams, tTJSVariant **param,
                        iTJSDispatch2 *objthis) override;

    tjs_error CreateNewByNum(tjs_uint32 flag, tjs_int num,
                             iTJSDispatch2 **result, tjs_int numparams,
                             tTJSVariant **param,
                             iTJSDispatch2 *objthis) override;

    tjs_error Reserved1() override;

    tjs_error IsInstanceOf(tjs_uint32 flag, const tjs_char *membername,
                           tjs_uint32 *hint, const tjs_char *classname,
                           iTJSDispatch2 *objthis) override;

    tjs_error IsInstanceOfByNum(tjs_uint32 flag, tjs_int num,
                                const tjs_char *classname,
                                iTJSDispatch2 *objthis) override;

    tjs_error Operation(tjs_uint32 flag, const tjs_char *membername,
                        tjs_uint32 *hint, tTJSVariant *result,
                        const tTJSVariant *param,
                        iTJSDispatch2 *objthis) override;

    tjs_error OperationByNum(tjs_uint32 flag, tjs_int num,
                             tTJSVariant *result, const tTJSVariant *param,
                             iTJSDispatch2 *objthis) override;

    tjs_error NativeInstanceSupport(tjs_uint32 flag, tjs_int32 classid,
                                    iTJSNativeInstance **pointer) override;

    tjs_error ClassInstanceInfo(tjs_uint32 flag, tjs_uint num,
                                tTJSVariant *value) override;

    tjs_error Reserved2() override;
    tjs_error Reserved3() override;

private:
    std::string m_className;
    std::string m_memberName;
    iTJSDispatch2 *m_original;
};

// ---------------------------------------------------------------------------
// PluginPropertyProxy — wraps a property dispatch, logs PropGet/PropSet then delegates
// ---------------------------------------------------------------------------
class PluginPropertyProxy : public tTJSDispatch {
public:
    PluginPropertyProxy(const std::string &className, const std::string &memberName,
                        iTJSDispatch2 *original);
    ~PluginPropertyProxy() override;

    tjs_uint AddRef() override;
    tjs_uint Release() override;

    tjs_error PropGet(tjs_uint32 flag, const tjs_char *membername,
                      tjs_uint32 *hint, tTJSVariant *result,
                      iTJSDispatch2 *objthis) override;

    tjs_error PropSet(tjs_uint32 flag, const tjs_char *membername,
                      tjs_uint32 *hint, const tTJSVariant *param,
                      iTJSDispatch2 *objthis) override;

    // Delegate everything else
    tjs_error FuncCall(tjs_uint32 flag, const tjs_char *membername,
                       tjs_uint32 *hint, tTJSVariant *result,
                       tjs_int numparams, tTJSVariant **param,
                       iTJSDispatch2 *objthis) override;

    tjs_error FuncCallByNum(tjs_uint32 flag, tjs_int num, tTJSVariant *result,
                            tjs_int numparams, tTJSVariant **param,
                            iTJSDispatch2 *objthis) override;

    tjs_error PropGetByNum(tjs_uint32 flag, tjs_int num, tTJSVariant *result,
                           iTJSDispatch2 *objthis) override;

    tjs_error PropSetByNum(tjs_uint32 flag, tjs_int num,
                           const tTJSVariant *param,
                           iTJSDispatch2 *objthis) override;

    tjs_error GetCount(tjs_int *result, const tjs_char *membername,
                       tjs_uint32 *hint, iTJSDispatch2 *objthis) override;

    tjs_error GetCountByNum(tjs_int *result, tjs_int num,
                            iTJSDispatch2 *objthis) override;

    tjs_error PropSetByVS(tjs_uint32 flag, tTJSVariantString *membername,
                          const tTJSVariant *param,
                          iTJSDispatch2 *objthis) override;

    tjs_error EnumMembers(tjs_uint32 flag, tTJSVariantClosure *callback,
                          iTJSDispatch2 *objthis) override;

    tjs_error DeleteMember(tjs_uint32 flag, const tjs_char *membername,
                           tjs_uint32 *hint, iTJSDispatch2 *objthis) override;

    tjs_error DeleteMemberByNum(tjs_uint32 flag, tjs_int num,
                                iTJSDispatch2 *objthis) override;

    tjs_error Invalidate(tjs_uint32 flag, const tjs_char *membername,
                         tjs_uint32 *hint, iTJSDispatch2 *objthis) override;

    tjs_error InvalidateByNum(tjs_uint32 flag, tjs_int num,
                              iTJSDispatch2 *objthis) override;

    tjs_error IsValid(tjs_uint32 flag, const tjs_char *membername,
                      tjs_uint32 *hint, iTJSDispatch2 *objthis) override;

    tjs_error IsValidByNum(tjs_uint32 flag, tjs_int num,
                           iTJSDispatch2 *objthis) override;

    tjs_error CreateNew(tjs_uint32 flag, const tjs_char *membername,
                        tjs_uint32 *hint, iTJSDispatch2 **result,
                        tjs_int numparams, tTJSVariant **param,
                        iTJSDispatch2 *objthis) override;

    tjs_error CreateNewByNum(tjs_uint32 flag, tjs_int num,
                             iTJSDispatch2 **result, tjs_int numparams,
                             tTJSVariant **param,
                             iTJSDispatch2 *objthis) override;

    tjs_error Reserved1() override;

    tjs_error IsInstanceOf(tjs_uint32 flag, const tjs_char *membername,
                           tjs_uint32 *hint, const tjs_char *classname,
                           iTJSDispatch2 *objthis) override;

    tjs_error IsInstanceOfByNum(tjs_uint32 flag, tjs_int num,
                                const tjs_char *classname,
                                iTJSDispatch2 *objthis) override;

    tjs_error Operation(tjs_uint32 flag, const tjs_char *membername,
                        tjs_uint32 *hint, tTJSVariant *result,
                        const tTJSVariant *param,
                        iTJSDispatch2 *objthis) override;

    tjs_error OperationByNum(tjs_uint32 flag, tjs_int num,
                             tTJSVariant *result, const tTJSVariant *param,
                             iTJSDispatch2 *objthis) override;

    tjs_error NativeInstanceSupport(tjs_uint32 flag, tjs_int32 classid,
                                    iTJSNativeInstance **pointer) override;

    tjs_error ClassInstanceInfo(tjs_uint32 flag, tjs_uint num,
                                tTJSVariant *value) override;

    tjs_error Reserved2() override;
    tjs_error Reserved3() override;

private:
    std::string m_className;
    std::string m_memberName;
    iTJSDispatch2 *m_original;
};

// ---------------------------------------------------------------------------
// PluginCallTracer — singleton managing the trace logger and proxy creation
// ---------------------------------------------------------------------------
class PluginCallTracer {
public:
    static PluginCallTracer &Instance();

    /// Create the file logger at the given path (call once at startup).
    void InitLogger(const std::string &logFilePath);

    /// Enable/disable tracing at runtime.
    void SetEnabled(bool enabled);

    bool IsEnabled() const { return m_enabled; }

    /// Direct logger access (used by proxy objects for diagnostics).
    void EnsureLogger();
    std::shared_ptr<spdlog::logger> GetLogger() const { return m_logger; }

    /// Wrap a dispatch object with the appropriate proxy.
    /// Returns the original pointer unchanged if tracing is disabled.
    iTJSDispatch2 *WrapDispatch(const ttstr &className, const ttstr &memberName,
                                iTJSDispatch2 *original,
                                tTJSNativeInstanceType type);

    // Log helpers called by proxy objects
    void LogMethodCall(const std::string &className, const std::string &memberName,
                       tjs_int numparams, tTJSVariant **param);

    void LogPropGet(const std::string &className, const std::string &memberName);

    void LogPropSet(const std::string &className, const std::string &memberName,
                    const tTJSVariant *value);

    // Registration phase logging
    void LogRegistrationStart();
    void LogRegistration(const ttstr &className, const ttstr &memberName,
                         tTJSNativeInstanceType type, tjs_uint32 flags);
    void LogRegistrationEnd();

    void LogPluginLoad(const std::string &name, bool success, const char *stub);

    /// Log a call to a member that does not exist (missing plugin function).
    /// operation is "FuncCall", "PropGet", or "PropSet".
    void LogMissingMember(const tjs_char *membername, const char *operation);

private:
    PluginCallTracer() = default;
    ~PluginCallTracer() = default;
    PluginCallTracer(const PluginCallTracer &) = delete;
    PluginCallTracer &operator=(const PluginCallTracer &) = delete;

    std::shared_ptr<spdlog::logger> m_logger;
    std::string m_logFilePath;
    std::mutex m_mutex;
    bool m_enabled = false;
    bool m_loggerInitialized = false;
};

#endif // AETHERKiri_PLUGIN_CALL_TRACER_HPP
