add_executable(${PROJECT_NAME} ${HATCH_SOURCES})

# emcc predefines __EMSCRIPTEN__; EMSCRIPTEN is the spelling the rest of the
# engine's platform checks use.
target_compile_definitions(${PROJECT_NAME} PRIVATE -DEMSCRIPTEN)

# The browser keeps OpenGL: WebGL 2 is OpenGL ES 3.0, which the GL renderer
# already has a path for. Open Asset Import is too much to carry into a page.
set(USE_OPEN_ASSET_IMPORT_LIBRARY OFF)

# SDL2, libpng and zlib are all Emscripten's own ports, built on demand and
# cached by the toolchain. They have to be named at compile time as well as at
# link time, so their headers are on the include path.
set(HATCH_EMSCRIPTEN_PORTS -sUSE_SDL=2 -sUSE_LIBPNG=1 -sUSE_ZLIB=1)
target_compile_options(${PROJECT_NAME} PRIVATE ${HATCH_EMSCRIPTEN_PORTS})

# Nothing is found on the build machine: the ports above are linked by the
# link options below, and WebGL is bound by Emscripten itself, so there is no
# loader to link and no GLEW to find.
set(SDL2_INCLUDE_DIRS "")
set(SDL2_LIBRARIES "")
set(PNG_FOUND TRUE)
set(PNG_LIBRARIES "")
set(OPENGL_LIBRARIES "")

# A .html suffix is what tells emcc to emit the page as well as the wasm and
# its loader, so the build produces something that can just be opened.
set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".html")

set(HATCH_WEB_PRELOAD "" CACHE PATH
    "A directory to bake into the page's filesystem, mounted at /")

target_link_options(${PROJECT_NAME} PRIVATE
  ${HATCH_EMSCRIPTEN_PORTS}

  # WebGL 2 is OpenGL ES 3.0, which is the profile the GL renderer's ES path
  # is written against. Asking for 2 as the minimum as well keeps the build
  # from quietly falling back to WebGL 1, where that path would not work.
  -sMIN_WEBGL_VERSION=2
  -sMAX_WEBGL_VERSION=2
  -sFULL_ES3=1

  # The engine sizes its allocations from the content it is given, which is
  # not known when the page is built.
  -sALLOW_MEMORY_GROWTH=1
  -sINITIAL_MEMORY=134217728

  # The bytecode VM recurses through the interpreter, and the default 64KB of
  # stack does not survive it.
  -sSTACK_SIZE=4194304

  # Emscripten drops the filesystem from a build that never mentions one at
  # link time, and the engine reaches for it at runtime rather than up front.
  # IDBFS is what makes saves outlive the tab.
  -sFORCE_FILESYSTEM=1
  -lidbfs.js

  # main() returns as soon as the browser loop is handed over, and the runtime
  # has to still be there afterwards to run it.
  -sEXIT_RUNTIME=0
  -sEXPORTED_RUNTIME_METHODS=['FS','callMain']

  # The script VM reports errors by throwing, and a build without exception
  # support turns every one of those into an abort.
  -fexceptions

  --shell-file "${CMAKE_SOURCE_DIR}/meta/web/shell.html"
)

# The VM throws; the compile has to be told so as well as the link.
target_compile_options(${PROJECT_NAME} PRIVATE -fexceptions)

# The page is an input to the link, but naming it in a link option does not
# tell CMake that, so editing it would otherwise leave the built page stale.
set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY
  LINK_DEPENDS "${CMAKE_SOURCE_DIR}/meta/web/shell.html")

if(HATCH_WEB_PRELOAD)
  target_link_options(${PROJECT_NAME} PRIVATE
    "--preload-file" "${HATCH_WEB_PRELOAD}@/")
endif()
