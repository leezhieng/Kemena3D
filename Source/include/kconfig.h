/**
 * @file kconfig.h
 * @brief Compile-time configuration switches for the Kemena3D engine.
 *
 * This header centralises preprocessor build flags that select which engine
 * features and rendering backends are compiled in. Enable a flag by defining
 * the corresponding macro (here or via the build system) before the engine
 * sources are compiled.
 */

#ifndef KCONFIG_H
#define KCONFIG_H

/**
 * @def K_RENDERER_OPENGL_ENABLED
 * @brief When defined, compiles in the OpenGL rendering backend.
 *
 * Disabled by default; uncomment or define via the build system to enable.
 */
// #define K_RENDERER_OPENGL_ENABLED

/**
 * @def K_RENDERER_VULKAN_ENABLED
 * @brief When defined, compiles in the Vulkan rendering backend.
 *
 * Disabled by default; uncomment or define via the build system to enable.
 */
// #define K_RENDERER_VULKAN_ENABLED

#endif // KCONFIG_H
