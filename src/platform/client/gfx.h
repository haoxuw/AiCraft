#pragma once

/**
 * Graphics backend include switch.
 *
 * AICRAFT_RHI_VK  → Vulkan headers (native only).
 * Otherwise       → OpenGL (GLAD on native, GLES3 on Emscripten).
 *
 * Replaces the former gl.h. Rendering code that still calls raw GL
 * functions includes this header — those calls will migrate to the RHI
 * interface over time (Phase 2–3).
 */

#ifdef AICRAFT_RHI_VK
  #ifndef GLFW_INCLUDE_VULKAN
  #define GLFW_INCLUDE_VULKAN
  #endif
  #include <GLFW/glfw3.h>
#else
  // GL backend (native + web)
  #ifdef __EMSCRIPTEN__
    #include <GLES3/gl3.h>
    #include <emscripten/html5.h>
  #else
    #include <glad/gl.h>
  #endif
#endif
