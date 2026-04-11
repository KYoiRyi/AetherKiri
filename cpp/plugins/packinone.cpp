#include "ncbind.hpp"
#include <spdlog/spdlog.h>

// The actual features of PackinOne.dll (fstat, dirlist, addFont, saveStruct, getMD5HashString)
// are already completely implemented in C++ across fstat/main.cpp, addFont.cpp, saveStruct.cpp, etc.
// They are registered globally at engine startup via TVPLoadInternalPlugins().
// We simply need to register the "packinone.dll" module identifier to satisfy the KAG script's linkage check.

class PackinOneDummy {
public:
    static void Stub() {
        // Dummy method
    }
};

#define NCB_MODULE_NAME TJS_W("packinone.dll")
NCB_REGISTER_CLASS(PackinOneDummy) {
    NCB_METHOD(Stub);
}
