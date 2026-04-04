#include "visual/ogl/ogl_common.h"

struct Live2DRenderTarget {
    GLuint fbo;
    GLsizei width;
    GLsizei height;
};

Live2DRenderTarget g_live2dRenderTarget = {0, 0, 0};

extern "C" void TVPRegisterKrkrLive2DPluginAnchor() {}
