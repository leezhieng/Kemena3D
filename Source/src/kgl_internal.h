/**
 * @file kgl_internal.h
 * @brief Internal header that selects the correct OpenGL / GLES headers.
 *
 * This header MUST NOT be included from public headers — it is meant for
 * .cpp files that call raw gl* functions directly (texture upload,
 * debug line rendering, etc.).  Public headers should use only the
 * kDriver abstraction.
 *
 * Selection logic:
 *  - KEMENA_GLES defined  → GLES3/gl3.h  (Android / mobile)
 *  - Desktop               → GLEW + GL/gl.h
 */

#ifndef KGL_INTERNAL_H
#define KGL_INTERNAL_H

#if defined(KEMENA_GLES) || defined(__ANDROID__)
  #include <GLES3/gl3.h>
  #include <GLES3/gl3platform.h>
#else
  #include <GL/glew.h>
  #ifdef __APPLE__
    #include <OpenGL/gl.h>
    #include <OpenGL/glu.h>
  #else
    #include <GL/gl.h>
    #include <GL/glu.h>
  #endif
#endif

#endif // KGL_INTERNAL_H
