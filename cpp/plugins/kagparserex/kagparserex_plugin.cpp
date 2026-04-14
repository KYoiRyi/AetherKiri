#include "tp_stub.h"
#include "ncbind.hpp"

static void LinkKAGParserCompatibility() {}
static void UnlinkKAGParserCompatibility() {}

// Register two compatibility module names:
// - KAGParserEx.dll (wamsoft)
// - ExtKAGParser.dll (older scripts)
static ncbCallbackAutoRegister g_kagparserex_cb(
    TJS_W("KAGParserEx.dll"), ncbAutoRegister::PreRegist,
    &LinkKAGParserCompatibility, nullptr);
static ncbCallbackAutoRegister g_kagparserex_unload_cb(
    TJS_W("KAGParserEx.dll"), ncbAutoRegister::PostRegist, nullptr,
    &UnlinkKAGParserCompatibility);
static ncbCallbackAutoRegister g_extkagparser_cb(
    TJS_W("ExtKAGParser.dll"), ncbAutoRegister::PreRegist,
    &LinkKAGParserCompatibility, nullptr);
static ncbCallbackAutoRegister g_extkagparser_unload_cb(
    TJS_W("ExtKAGParser.dll"), ncbAutoRegister::PostRegist, nullptr,
    &UnlinkKAGParserCompatibility);
