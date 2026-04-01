#pragma once

/**
 * Platform-agnostic OpenGL include.
 *
 * Native: GLAD for GL 4.1 Core.
 * Web (Emscripten): built-in GL ES 3.0 headers.
 */

#ifdef __EMSCRIPTEN__
  #include <GLES3/gl3.h>
  #include <emscripten/html5.h>
#else
  #include <glad/gl.h>
#endif
