#ifndef ENGINE_RENDERER_GL_INCLUDES
#define ENGINE_RENDERER_GL_INCLUDES

// OpenGL bindings
#if MACOSX
    #include <OpenGL/gl3.h>
    #include <OpenGL/gl3ext.h>
#elif SWITCH
    #include <GLES2/gl2.h>
    #include <SDL2/SDL_opengl.h>
    #include <GLES2/gl2ext.h>
#elif IOS
    #include <OpenGLES/ES3/gl.h>
    #include <OpenGLES/ES3/glext.h>
#elif ANDROID
    #include <SDL_opengles2.h>
#elif EMSCRIPTEN
    // WebGL 2 is OpenGL ES 3.0. There is nothing to load: the browser provides
    // the implementation and Emscripten binds it directly, so no GLEW here.
    #include <GLES3/gl3.h>
    #include <GLES2/gl2ext.h>
#else
    #include <GL/glew.h>
    #define USING_GLEW
#endif

// Which OpenGL got included is only known here, so the answer is settled here
// too, rather than in each file that needs it. It used to be worked out inside
// GLRenderer.cpp, which meant the shader builder -- the one other place that
// asks -- never saw it and silently built desktop GLSL for ES targets.
#if GL_ES_VERSION_2_0 || GL_ES_VERSION_3_0
    #define GL_ES
#endif

#endif /* ENGINE_RENDERER_GL_INCLUDES */
