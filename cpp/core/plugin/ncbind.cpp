#include "ncbind.hpp"
#include "PluginCallTracer.hpp"
#include <set>
#include <spdlog/spdlog.h>

// static変数の実体

// auto register 先頭ポインタ
ncbAutoRegister::ThisClassT const*
ncbAutoRegister::_top[ncbAutoRegister::LINE_COUNT] = NCB_INNER_AUTOREGISTER_LINES_INSTANCE;

std::map<ttstr, ncbAutoRegister::INTERNAL_PLUGIN_LISTS > ncbAutoRegister::_internal_plugins;

bool ncbAutoRegister::LoadModule(const ttstr &_name)
{
	ttstr name = _name.AsLowerCase();
	if (TVPRegisteredPlugins.find(name) != TVPRegisteredPlugins.end()) {
        spdlog::trace("ncbAutoRegister::LoadModule('{}'): already registered",
                      name.AsStdString());
		return true;
    }
	auto it = _internal_plugins.find(name);
	if (it != _internal_plugins.end()) {
        PluginCallTracer::Instance().LogModuleStart(name.AsStdString());
        spdlog::trace("ncbAutoRegister::LoadModule('{}'): found internal module",
                      name.AsStdString());
		for (int line = 0; line < LINE_COUNT; ++line) {
            const auto &plugin_list = it->second.lists[line];
            for (auto i : plugin_list) {
                const ttstr module = i->modulename ? ttstr(i->modulename) : ttstr();
                spdlog::trace(
                    "ncbAutoRegister::LoadModule('{}'): Regist begin line={} entry='{}'",
                    name.AsStdString(), line, module.AsStdString());
                try {
				    i->Regist();
                } catch(...) {
                    spdlog::error(
                        "ncbAutoRegister::LoadModule('{}'): Regist threw at line={} entry='{}'",
                        name.AsStdString(), line, module.AsStdString());
                    throw;
                }
                spdlog::trace(
                    "ncbAutoRegister::LoadModule('{}'): Regist end line={} entry='{}'",
                    name.AsStdString(), line, module.AsStdString());
			}
		}
		TVPRegisteredPlugins.insert(name);
        spdlog::trace("ncbAutoRegister::LoadModule('{}'): regist complete",
                      name.AsStdString());
		return true;
	}
    spdlog::warn("ncbAutoRegister::LoadModule('{}'): module not found in internal plugin map",
                 name.AsStdString());
	return false;
}

bool ncbAutoRegister::HasModule(const ttstr &_name)
{
	ttstr name = _name.AsLowerCase();
	return _internal_plugins.find(name) != _internal_plugins.end();
}

void ncbAutoRegister::LoadAllModules()
{
    spdlog::trace("ncbAutoRegister::LoadAllModules: begin ({} modules in map)",
                  static_cast<int>(_internal_plugins.size()));
	for (auto &kv : _internal_plugins) {
		const ttstr &name = kv.first;
		if (TVPRegisteredPlugins.find(name) != TVPRegisteredPlugins.end())
			continue;
        PluginCallTracer::Instance().LogModuleStart(name.AsStdString());
        spdlog::trace("ncbAutoRegister::LoadAllModules: register '{}'",
                      name.AsStdString());
		for (int line = 0; line < LINE_COUNT; ++line) {
            const auto &plugin_list = kv.second.lists[line];
			for (auto i : plugin_list) {
                const ttstr module = i->modulename ? ttstr(i->modulename) : ttstr();
                spdlog::trace(
                    "ncbAutoRegister::LoadAllModules('{}'): Regist begin line={} entry='{}'",
                    name.AsStdString(), line, module.AsStdString());
                try {
				    i->Regist();
                } catch(...) {
                    spdlog::error(
                        "ncbAutoRegister::LoadAllModules('{}'): Regist threw at line={} entry='{}'",
                        name.AsStdString(), line, module.AsStdString());
                    throw;
                }
                spdlog::trace(
                    "ncbAutoRegister::LoadAllModules('{}'): Regist end line={} entry='{}'",
                    name.AsStdString(), line, module.AsStdString());
			}
		}
		TVPRegisteredPlugins.insert(name);
        spdlog::trace("ncbAutoRegister::LoadAllModules: module '{}' done",
                      name.AsStdString());
	}
    spdlog::trace("ncbAutoRegister::LoadAllModules: end");
}

void ncbAutoRegister::ResetRegistrationState()
{
    _internal_plugins.clear();
}
