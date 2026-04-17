//
// Reverse-engineered from libkrkr2.so D3DEmoteModule class
// Top-level class registered under emoteplayer.dll
//
#pragma once

namespace motion {

    class D3DEmoteModule {
    public:
        D3DEmoteModule() = default;

        static void setMaskMode(int v) { _maskMode = v; }
        static int getMaskMode() { return _maskMode; }

        static void setMaskRegionClipping(bool v) { _maskRegionClipping = v; }
        static bool getMaskRegionClipping() { return _maskRegionClipping; }

        static void setMipMapEnabled(bool v) { _mipMapEnabled = v; }
        static bool getMipMapEnabled() { return _mipMapEnabled; }

        static void setAlphaOp(int v) { _alphaOp = v; }
        static int getAlphaOp() { return _alphaOp; }

        static void setProtectTranslucentTextureColor(bool v) {
            _protectTranslucentTextureColor = v;
        }
        static bool getProtectTranslucentTextureColor() {
            return _protectTranslucentTextureColor;
        }

        static void setPixelateDivision(int v) { _pixelateDivision = v; }
        static int getPixelateDivision() { return _pixelateDivision; }

        static void setMaxTextureSize(int w, int h) {
            _maxTextureWidth = w > 0 ? w : 0;
            _maxTextureHeight = h > 0 ? h : 0;
        }

    private:
        inline static int _maskMode = 1; // MaskModeAlpha
        inline static bool _maskRegionClipping = false;
        inline static bool _mipMapEnabled = false;
        inline static int _alphaOp = 0;
        inline static bool _protectTranslucentTextureColor = false;
        inline static int _pixelateDivision = 1;
        inline static int _maxTextureWidth = 0;
        inline static int _maxTextureHeight = 0;
    };

} // namespace motion
