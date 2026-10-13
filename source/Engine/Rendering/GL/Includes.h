#ifndef ENGINE_RENDERER_GL_INCLUDES
#define ENGINE_RENDERER_GL_INCLUDES

// OpenGL bindings
#if MACOSX
#include <OpenGL/gl3.h>
#include <OpenGL/gl3ext.h>
#elif SWITCH
#include <SDL_opengles2.h>
#elif IOS
#include <OpenGLES/ES3/gl.h>
#include <OpenGLES/ES3/glext.h>
#elif ANDROID
#include <SDL_opengles2.h>
#elif EMSCRIPTEN
// WebGL 2 is OpenGL ES 3.0. There is nothing to load: the browser provides
// the implementation and Emscripten binds it directly, so no GLEW here.
// gl3.h first: gl2ext.h only declares extensions to types gl3.h defines.
// clang-format off
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
// clang-format on
#else
#include <GL/glew.h>
#define USING_GLEW
#endif

// Which OpenGL got included is only known here, so the answer is settled here
// too, for every file that asks rather than only those that include the
// renderer's own header.
#if GL_ES_VERSION_2_0 || GL_ES_VERSION_3_0
#define GL_ES
#endif

#endif /* ENGINE_RENDERER_GL_INCLUDES */
