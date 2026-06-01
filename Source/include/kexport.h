/**
 * @file kexport.h
 * @brief Symbol visibility / linkage macro for the Kemena3D library.
 *
 * Defines the @c KEMENA3D_API macro used to annotate public classes and
 * functions so they are correctly exported from or imported into a shared
 * library, with platform-specific handling for Windows (MSVC/Cygwin) and
 * GCC/Clang-style toolchains.
 */
#pragma once

// Detect Windows
#if defined(_WIN32) || defined(__CYGWIN__)
  #if defined(KEMENA3D_SHARED)
    /**
     * @brief Public symbol decoration macro.
     *
     * On Windows when building the shared library (@c KEMENA3D_SHARED),
     * expands to @c __declspec(dllexport) to export the symbol; when consuming
     * it (@c KEMENA3D_IMPORT), expands to @c __declspec(dllimport); otherwise
     * (static build) it expands to nothing. On non-Windows platforms it expands
     * to @c __attribute__((visibility("default"))) for shared/import builds and
     * to nothing for static builds.
     */
    #define KEMENA3D_API __declspec(dllexport)
  #elif defined(KEMENA3D_IMPORT)
    #define KEMENA3D_API __declspec(dllimport)
  #else
    #define KEMENA3D_API
  #endif
#else
  #if defined(KEMENA3D_SHARED) || defined(KEMENA3D_IMPORT)
    #define KEMENA3D_API __attribute__((visibility("default")))
  #else
    #define KEMENA3D_API
  #endif
#endif