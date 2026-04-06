#include "FontImpl.h"
#include <ft2build.h>
#include FT_TRUETYPE_IDS_H
#include FT_SFNT_NAMES_H
#include FT_FREETYPE_H
#include "StorageIntf.h"
#include "DebugIntf.h"
#include "MsgIntf.h"
#include <map>
#include <cmath>
#include "Application.h"
#include "Platform.h"
#include "ConfigManager/IndividualConfigManager.h"
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <fstream>
#include <vector>
#include "StorageImpl.h"
#include "BinaryStream.h"
#include <spdlog/spdlog.h>
#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_IOS
#include <CoreText/CoreText.h>
#include <CoreFoundation/CoreFoundation.h>
#endif
#endif

tTJSHashTable<ttstr, TVPFontNamePathInfo, tTVPttstrHash> TVPFontNames;
static ttstr TVPDefaultFontName;
const ttstr &TVPGetDefaultFontName() { return TVPDefaultFontName; }
void TVPGetAllFontList(std::vector<ttstr> &list) {
    auto itend = TVPFontNames.GetLast();
    for(auto it = TVPFontNames.GetFirst(); it != itend; ++it) {
        list.push_back(it.GetKey());
    }
}

static FT_Library TVPFontLibrary;

const FT_Library TVPGetFontLibrary() {

    if(!TVPFontLibrary) {
        FT_Error error = FT_Init_FreeType(&TVPFontLibrary);
        if(error)
            TVPThrowExceptionMessage(
                (ttstr(TJS_W("Initialize FreeType failed, error = ")) +
                 TJSIntegerToString((tjs_int)error))
                    .c_str());
        TVPInitFontNames();
    }
    return TVPFontLibrary;
}

void TVPReleaseFontLibrary() {
    if(TVPFontLibrary) {
        FT_Done_FreeType(TVPFontLibrary);
    }
}
//---------------------------------------------------------------------------
static int TVPInternalEnumFonts(
    FT_Byte *pBuf, int buflen, const ttstr &FontPath,
    const std::function<tTJSBinaryStream *(TVPFontNamePathInfo *)> &getter) {
    unsigned int faceCount = 0;
    FT_Face fontface;
    FT_Error error =
        FT_New_Memory_Face(TVPGetFontLibrary(), pBuf, buflen, 0, &fontface);
    if(error) {
        TVPAddLog(ttstr(TJS_W("Load Font \"") + FontPath + "\" failed (" +
                        TJSIntegerToString((int)error) + ")"));
        return faceCount;
    }
    int nFaceNum = fontface->num_faces;
    for(int i = 0; i < nFaceNum; ++i) {
        if(i > 0) {
            if(FT_New_Memory_Face(TVPGetFontLibrary(), pBuf, buflen, i,
                                  &fontface)) {
                continue;
            }
        }
        if(FT_IS_SCALABLE(fontface)) {
            FT_UInt namecount = FT_Get_Sfnt_Name_Count(fontface);
            int addCount = 0;
            for(FT_UInt j = 0; j < namecount; ++j) {
                FT_SfntName name;
                if(FT_Get_Sfnt_Name(fontface, j, &name)) {
                    continue;
                }
                if(name.name_id != TT_NAME_ID_FONT_FAMILY) {
                    continue;
                }
                if(name.platform_id != TT_PLATFORM_MICROSOFT) {
                    continue;
                }
                switch(name.language_id) { // for CJK names
                    case TT_MS_LANGID_JAPANESE_JAPAN:
                    case TT_MS_LANGID_CHINESE_GENERAL:
                    case TT_MS_LANGID_CHINESE_TAIWAN:
                    case TT_MS_LANGID_CHINESE_PRC:
                    case TT_MS_LANGID_CHINESE_HONG_KONG:
                    case TT_MS_LANGID_CHINESE_SINGAPORE:
                    case TT_MS_LANGID_KOREAN_EXTENDED_WANSUNG_KOREA:
                    case TT_MS_LANGID_KOREAN_JOHAB_KOREA:
                        break;
                    default:
                        continue;
                }
                ttstr fontname;
                if(name.encoding_id == TT_MS_ID_UNICODE_CS) {
                    std::vector<tjs_char> tmp;
                    int namelen = name.string_len / 2;
                    tmp.resize(namelen + 1);
                    for(int k = 0; k < namelen; ++k) {
                        tmp[k] = (name.string[k * 2] << 8) |
                            (name.string[k * 2 + 1]);
                    }
                    fontname = &tmp.front();
                } else {
                    continue;
                }
                TVPFontNamePathInfo info;
                info.Path = FontPath;
                info.Index = j;
                info.Getter = getter;
                TVPFontNames.Add(fontname, info);
                addCount = 1;
            }
            /*if (!addCount)*/ {
                ttstr fontname((tjs_nchar *)fontface->family_name);
                TVPFontNamePathInfo info;
                info.Path = FontPath;
                info.Index = i;
                info.Getter = getter;
                TVPFontNames.Add(fontname, info);
            }
            ++faceCount;
        }

        FT_Done_Face(fontface);
    }
    return faceCount;
}

/**
 *  only support little word!!!
 * @param FontPath font path str
 * @return load failed return 0, otherwise > 0
 */
int TVPEnumFontsProc(const ttstr &FontPath) {
    if(!TVPIsExistentStorageNoSearch(FontPath)) {
        return 0;
    }

    tTJSBinaryStream *Stream = TVPCreateStream(FontPath, TJS_BS_READ);
    if(!Stream) {
        return 0;
    }
    int bufflen = Stream->GetSize();
    std::vector<FT_Byte> buf;
    buf.resize(bufflen);
    Stream->ReadBuffer(&buf.front(), bufflen);
    delete Stream;
    return TVPInternalEnumFonts(&buf.front(), bufflen, FontPath, nullptr);
}

tTJSBinaryStream *TVPCreateFontStream(const ttstr &fontname) {
    TVPFontNamePathInfo *info = TVPFindFont(fontname);
    if(!info) {
        info = TVPFontNames.Find(TVPDefaultFontName);
        if(!info)
            return nullptr;
    }
    if(info->Getter) {
        return info->Getter(info);
    }
    return TVPCreateBinaryStreamForRead(info->Path, TJS_W(""));
}

//---------------------------------------------------------------------------
#ifdef __ANDROID__
extern std::vector<ttstr> Android_GetExternalStoragePath();
extern ttstr Android_GetInternalStoragePath();
extern ttstr Android_GetApkStoragePath();
#endif
void TVPInitFontNames() {
    static bool TVPFontNamesInit = false;
    // enumlate all fonts
    if(TVPFontNamesInit)
        return;
    TVPFontNamesInit = true;
#ifdef __ANDROID__
    std::vector<ttstr> pathlist = Android_GetExternalStoragePath();
#endif
    do {
        ttstr userFont =
            IndividualConfigManager::GetInstance()->GetValue<std::string>(
                "default_font", "");
        if(!userFont.IsEmpty() && TVPEnumFontsProc(userFont))
            break;

        if(TVPEnumFontsProc(TVPGetAppPath() + "default.ttf"))
            break;
        if(TVPEnumFontsProc(TVPGetAppPath() + "default.ttc"))
            break;
        if(TVPEnumFontsProc(TVPGetAppPath() + "default.otf"))
            break;
        if(TVPEnumFontsProc(TVPGetAppPath() + "default.otc"))
            break;
#if defined(__ANDROID__)
        int fontCount = 0;
        for(const ttstr &path : pathlist) {
            fontCount += TVPEnumFontsProc(path + "/default.ttf");
            if(fontCount)
                break;
        }
        if(fontCount)
            break;

        if(TVPEnumFontsProc(Android_GetInternalStoragePath() + "/default.ttf"))
            break;

#elif defined(WIN32)
        if(TVPEnumFontsProc(TJS_W("file://./c/Windows/Fonts/msyh.ttf")))
            break;
        if(TVPEnumFontsProc(TJS_W("file://./c/Windows/Fonts/simhei.ttf")))
            break;
#elif defined(__APPLE__)
#if TARGET_OS_IOS
        // iOS: system fonts cannot be accessed via the engine's file://
        // storage system due to sandbox and path-normalization (lowercase).
        // Use direct POSIX reads via tryReadFont below instead.
        // (falls through to the "internal storage" block)
#else
        // macOS: system fonts are accessible via the engine's storage layer
        if(TVPEnumFontsProc(TJS_W("file://./System/Library/Fonts/PingFang.ttc")))
            break;
        if(TVPEnumFontsProc(
               TJS_W("file://./System/Library/Fonts/Hiragino Sans GB.ttc")))
            break;
        if(TVPEnumFontsProc(
               TJS_W("file://./System/Library/Fonts/Supplemental/Arial Unicode.ttf")))
            break;
#endif
#endif

        { // from internal storage (or system fonts on iOS)
            // Read font file using standard file I/O
            auto tryReadFont = [](const std::string &path) -> std::vector<uint8_t> {
                std::ifstream ifs(path, std::ios::binary | std::ios::ate);
                if (!ifs.is_open()) return {};
                auto size = ifs.tellg();
                if (size <= 0) return {};
                std::vector<uint8_t> data(static_cast<size_t>(size));
                ifs.seekg(0);
                ifs.read(reinterpret_cast<char*>(data.data()), size);
                return data;
            };

            // Helper: load a font from POSIX path and register it.
            auto tryLoadFontDirect = [&tryReadFont](const std::string &path,
                                                     const std::string &label) -> bool {
                auto fdata = tryReadFont(path);
                if (fdata.empty()) return false;
                spdlog::info("loaded system font: {}", path);
                return TVPInternalEnumFonts(
                    fdata.data(), fdata.size(), label.c_str(),
                    [&tryReadFont](TVPFontNamePathInfo *info) -> tTJSBinaryStream * {
                        auto d = tryReadFont(info->Path.AsStdString());
                        if (d.empty()) return nullptr;
                        auto *ret = new tTVPMemoryStream();
                        ret->WriteBuffer(d.data(), d.size());
                        ret->SetPosition(0);
                        return ret;
                    }) > 0;
            };

#if defined(__ANDROID__)
            if(tryLoadFontDirect("/system/fonts/NotoSansSC-Regular.otf",
                                 "/system/fonts/NotoSansSC-Regular.otf"))
                break;
            if(tryLoadFontDirect("/system/fonts/DroidSansFallback.ttf",
                                 "/system/fonts/DroidSansFallback.ttf"))
                break;
            if(tryLoadFontDirect("/system/fonts/NotoSansCJK-Regular.ttc",
                                 "/system/fonts/NotoSansCJK-Regular.ttc"))
                break;
#endif

#if defined(__APPLE__) && TARGET_OS_IOS
            // iOS: use CoreText API to get system font data (sandbox-safe).
            {
                // Preferred font names in order: PingFang SC (CN), Hiragino Sans (JP)
                static const char *preferredFonts[] = {
                    "PingFangSC-Regular",
                    "HiraginoSans-W3",
                    "HiraMinProN-W3",
                    nullptr
                };
                bool loaded = false;
                for (const char **fname = preferredFonts; *fname && !loaded; ++fname) {
                    CFStringRef fontName = CFStringCreateWithCString(
                        kCFAllocatorDefault, *fname, kCFStringEncodingUTF8);
                    if (!fontName) continue;

                    CTFontRef ctFont = CTFontCreateWithName(fontName, 12.0, nullptr);
                    CFRelease(fontName);
                    if (!ctFont) continue;

                    // Get the font URL from the CTFont descriptor
                    CTFontDescriptorRef desc = CTFontCopyFontDescriptor(ctFont);
                    CFURLRef fontURL = desc
                        ? (CFURLRef)CTFontDescriptorCopyAttribute(desc, kCTFontURLAttribute)
                        : nullptr;

                    if (fontURL) {
                        char pathBuf[1024];
                        if (CFURLGetFileSystemRepresentation(fontURL, true,
                                (UInt8 *)pathBuf, sizeof(pathBuf))) {
                            std::string fontPath(pathBuf);
                            spdlog::info("iOS CoreText font path: {}", fontPath);
                            if (tryLoadFontDirect(fontPath, fontPath)) {
                                loaded = true;
                            }
                        }
                        CFRelease(fontURL);
                    }
                    if (desc) CFRelease(desc);
                    CFRelease(ctFont);
                }
                if (loaded) break;
            }
#endif

            auto data = tryReadFont("NotoSansCJK-Regular.ttc");
            if (data.empty()) {
                data = tryReadFont("fonts/NotoSansCJK-Regular.ttc");
            }
            if (data.empty()) {
                spdlog::warn("internal font file not found: NotoSansCJK-Regular.ttc");
            } else if(TVPInternalEnumFonts(
                   data.data(), data.size(), "NotoSansCJK-Regular.ttc",
                   [&tryReadFont](TVPFontNamePathInfo *info) -> tTJSBinaryStream * {
                       auto fdata = tryReadFont(info->Path.AsStdString());
                       if (fdata.empty()) return nullptr;
                       auto *ret = new tTVPMemoryStream();
                       ret->WriteBuffer(fdata.data(), fdata.size());
                       ret->SetPosition(0);
                       return ret;
                   }))
                break;
        }
    } while(false);
    if(TVPFontNames.GetCount() > 0) {
        // set default fontface name
        TVPDefaultFontName = TVPFontNames.GetLast().GetKey();
    }

    // check exePath + "/fonts/*.ttf"
    {
        std::vector<ttstr> list;
        auto lister = [&](const ttstr &name, tTVPLocalFileInfo *s) {
            if(s->Mode & (S_IFREG | S_IFDIR)) {
                list.emplace_back(name);
            }
        };
#ifdef __ANDROID__
        TVPGetLocalFileListAt(Android_GetInternalStoragePath() + "/fonts",
                              lister);
        for(const ttstr &path : pathlist) {
            TVPGetLocalFileListAt(path + "/fonts", lister);
        }
#endif
        TVPGetLocalFileListAt(TVPGetAppPath() + "/fonts", lister);
        auto itend = list.end();
        for(auto it = list.begin(); it != itend; ++it) {
            TVPEnumFontsProc(*it);
        }
    }

    if(TVPDefaultFontName.IsEmpty()) {
        TVPShowSimpleMessageBox(
            ("Could not found any font.\nPlease ensure that at "
             "least \"default.ttf\" exists"),
            "Exception Occured");
    }
}
//---------------------------------------------------------------------------
TVPFontNamePathInfo *TVPFindFont(const ttstr &fontname) {
    // check existence of font
    TVPInitFontNames();

    TVPFontNamePathInfo *info = nullptr;
    if(!fontname.IsEmpty() && fontname[0] == TJS_W('@')) { // vertical version
        info = TVPFontNames.Find(fontname.c_str() + 1);
    }
    if(!info) {
        info = TVPFontNames.Find(fontname);
    }
    return info;
}

tjs_uint32 tTVPttstrHash::Make(const ttstr &val) {
    const tjs_char *ptr = val.c_str();
    if(*ptr == 0)
        return 0;
    tjs_uint32 v = 0;
    while(*ptr) {
        v += *ptr;
        v += (v << 10);
        v ^= (v >> 6);
        ptr++;
    }
    v += (v << 3);
    v ^= (v >> 11);
    v += (v << 15);
    if(!v)
        v = (tjs_uint32)-1;
    return v;
}
