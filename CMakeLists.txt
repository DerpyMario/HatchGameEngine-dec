cmake_minimum_required(VERSION 3.16)

# set the project name and version
project(HatchGameEngine VERSION 1.2.2)

# specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
  set(CMAKE_CXX_FLAGS "/wd4244 /wd4267 /wd4305 /wd4717 /EHsc")
else()
  set(CMAKE_CXX_FLAGS_RELEASE "-Ofast")

  # -march=nocona names an x86 processor, so it is only a flag the compiler
  # will take on x86, and not even there when several architectures are being
  # built into the one binary.
  # Emscripten reports an x86 processor even though what it emits is wasm, so it
  # has to be excluded by name rather than by the processor check.
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64|i[3-6]86)$"
     AND NOT CMAKE_OSX_ARCHITECTURES AND NOT EMSCRIPTEN)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -march=nocona")
  endif()
endif()

# Set up CMake modules path
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")

file(GLOB_RECURSE C_SOURCES RELATIVE ${CMAKE_SOURCE_DIR} "source/*.c")
file(GLOB_RECURSE CPP_SOURCES RELATIVE ${CMAKE_SOURCE_DIR} "source/*.cpp")

set(HATCH_SOURCES ${C_SOURCES} ${CPP_SOURCES})

# The Xbox reaches its GPU through nxdk's pbkit, so the OpenGL renderer has
# nothing to talk to and its headers are not there to be found.
if(NXDK)
  list(FILTER HATCH_SOURCES EXCLUDE REGEX "source/Engine/Rendering/GL/")

  # The triangulation library reports degenerate input by throwing, and nxdk
  # has no exceptions to throw. Rather than turn every one of those into an
  # abort, it is left out; Geometry::Triangulate says so and returns nothing.
  list(FILTER HATCH_SOURCES EXCLUDE REGEX "source/Libraries/poly2tri/")
endif()

# The macOS half of the filesystem code is Objective-C, since the application
# support directory can only be asked for through Foundation.
if(APPLE)
  enable_language(OBJC)
  file(GLOB_RECURSE OBJC_SOURCES RELATIVE ${CMAKE_SOURCE_DIR} "source/*.m")
  list(APPEND HATCH_SOURCES ${OBJC_SOURCES})
endif()

set(HATCH_TARGET_NAME ${PROJECT_NAME})
if(TARGET_NAME)
  set(HATCH_TARGET_NAME ${TARGET_NAME})
endif()

# NXDK is set by nxdk's toolchain file. It also sets WIN32, since the Xbox is
# given a subset of the Windows API, so the Xbox has to be asked about first or
# it would be built as a PC.
if(NXDK)
  # SDL_MAIN_HANDLED is not set here: nxdk's SDL defines it itself, and setting
  # it again only earns a redefinition warning in every translation unit.
  add_definitions(-DXBOX -DTARGET_NAME=\"${HATCH_TARGET_NAME}\"
   -DNO_LIBAV
  )
elseif(EMSCRIPTEN)
  # Asked about before the catch-all below, which would otherwise call the
  # browser Linux: Emscripten is a Unix as far as CMake's system name goes.
  # emcc predefines __EMSCRIPTEN__; EMSCRIPTEN is the spelling the rest of the
  # engine's platform checks use.
  add_definitions(-DEMSCRIPTEN -DTARGET_NAME=\"${HATCH_TARGET_NAME}\"
   -DNO_LIBAV
  )
elseif(APPLE)
  add_definitions(-DMACOSX -DTARGET_NAME=\"${HATCH_TARGET_NAME}\"
   -DNO_LIBAV
  )
elseif(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Windows")
  add_definitions(-DLINUX -DTARGET_NAME=\"${HATCH_TARGET_NAME}\"
   -DNO_LIBAV
  )
else()
  # Cross compiling a Windows executable
  add_definitions(-DWIN32 -DTARGET_NAME=\"${HATCH_TARGET_NAME}\"
   -DNO_LIBAV
   -DGLEW_STATIC
   -D_WINDOWS
  )
endif()

# Build options
option(ENABLE_SCRIPT_COMPILING "Enable script compiling" ON)

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
  option(WINDOWS_USE_RESOURCE_FILE "Use resource file (Windows)" ON)
endif()

option(USE_OPEN_ASSET_IMPORT_LIBRARY "Use Open Asset Import Library" ON)
option(USE_FREETYPE_LIBRARY "Use FreeType" OFF)

# Builds against an SDL2 that was built from source and installed next to this
# one, through the CMake package SDL2 installs, which knows every library its
# static build needs. This is how the release workflow gets an executable that
# carries SDL2 with it instead of asking for it to be installed, and how it
# reaches architectures the bundled libraries do not cover.
option(USE_SDL2_CONFIG "Find SDL2 through its own CMake package" OFF)

# The same idea for GLEW, which is only bundled for 64-bit x86 Windows.
set(GLEW_LIBRARY "" CACHE FILEPATH "GLEW library to link instead of the bundled one")
set(GLEW_INCLUDE_DIR "" CACHE PATH "Where that GLEW's headers are")

if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
  set(USE_OPEN_ASSET_IMPORT_LIBRARY OFF)
endif()

# Renderers
option(USING_OPENGL "Use OpenGL" ON)

# The Xbox's GPU is reached through nxdk's own pbkit rather than OpenGL, so the
# engine falls back to what it already has for machines without it: the SDL2
# backend over its own software rasteriser. Open Asset Import is far too much
# for the machine, and the scripts are compiled ahead of time on a PC.
if(NXDK)
  # See the header for why this is forced into everything.
  add_compile_options(-include "${CMAKE_SOURCE_DIR}/source/Engine/Includes/XboxCompat.h")

  set(USING_OPENGL OFF)
  set(USE_OPEN_ASSET_IMPORT_LIBRARY OFF)
  set(USE_FREETYPE_LIBRARY OFF)
endif()

# The browser keeps OpenGL: WebGL 2 is OpenGL ES 3.0, which the GL renderer
# already has a path for, and everything is reached through SDL2 and the
# libraries Emscripten ports rather than anything found on the machine.
if(EMSCRIPTEN)
  set(USE_OPEN_ASSET_IMPORT_LIBRARY OFF)
  set(USE_FREETYPE_LIBRARY OFF)

  # Ports are built on demand and cached, and have to be named at compile time
  # as well as link time so their headers are on the include path.
  set(HATCH_EMSCRIPTEN_PORTS -sUSE_SDL=2 -sUSE_LIBPNG=1 -sUSE_ZLIB=1)
  add_compile_options(${HATCH_EMSCRIPTEN_PORTS})
endif()
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
  option(USING_DIRECT3D "Use Direct3D" OFF)
endif()

if(USE_FREETYPE_LIBRARY)
  add_definitions(-DUSING_FREETYPE)
endif()

if(USING_OPENGL)
  add_definitions(-DUSING_OPENGL)
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows" AND USING_DIRECT3D)
  add_definitions(-DUSING_DIRECT3D)
endif()

if(NOT ENABLE_SCRIPT_COMPILING)
  add_definitions(-DNO_SCRIPT_COMPILING)
endif()

add_definitions(-DMINIZ_NO_ARCHIVE_APIS -DMINIZ_NO_ARCHIVE_WRITING_APIS -DMINIZ_NO_TIME )

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
  set(MAKEHEADERS_BINARY makeheaders.exe)
else()
  set(MAKEHEADERS_BINARY makeheaders)
endif()

add_custom_target(makeheaders ALL
  COMMAND ${CMAKE_SOURCE_DIR}/tools/${MAKEHEADERS_BINARY} source/
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  BYPRODUCTS "${CMAKE_SOURCE_DIR}/include/makeheaders.bin" "${CMAKE_SOURCE_DIR}/include/Engine"
  COMMENT "Generating header files")

find_file(REPO_DIR NAMES .git PATHS ${CMAKE_SOURCE_DIR} NO_DEFAULT_PATH)

if (REPO_DIR)
  execute_process(
    COMMAND git log -1 --format=%H
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE)

  add_definitions("-DGIT_COMMIT_HASH=\"${GIT_COMMIT_HASH}\"")
endif()

add_executable(${PROJECT_NAME} ${HATCH_SOURCES})
add_dependencies(${PROJECT_NAME} makeheaders)

set(OUT_EXEC_NAME ${PROJECT_NAME})

# Change executable name
if(EXECUTABLE_NAME)
  set(OUT_EXEC_NAME ${EXECUTABLE_NAME})
endif()
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${OUT_EXEC_NAME}-${CMAKE_BUILD_TYPE}")

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
  set(WIN_RES_FILE meta/win/icon.rc)

  if(RESOURCE_FILE)
    set(WIN_RES_FILE ${RESOURCE_FILE})
  endif()
endif()

# SDL2 from its own CMake package, which carries a target that already knows
# what a static SDL2 has to be linked against on this platform.
if(USE_SDL2_CONFIG)
  find_package(SDL2 CONFIG REQUIRED)

  if(TARGET SDL2::SDL2-static)
    set(SDL2_LIBRARIES SDL2::SDL2-static)
  else()
    set(SDL2_LIBRARIES SDL2::SDL2)
  endif()

  set(SDL2_INCLUDE_DIRS "")
endif()

# Find the includes
if(NXDK)
  set(SDL2_INCLUDE_DIRS "${NXDK_DIR}/lib/sdl/SDL2/include")
  set(SDL2_LIBRARIES "${NXDK_DIR}/lib/libSDL2.lib")

  add_definitions(-DUSING_LIBPNG -DUSING_LIBPNG_HEADER=<png.h>)
  set(PNG_FOUND TRUE)
  set(PNG_LIBRARIES "${NXDK_DIR}/lib/libpng.lib" "${NXDK_DIR}/lib/libzlib.lib")
  # nxdk keeps libpng's generated configuration a level above the sources.
  include_directories("${NXDK_DIR}/lib/libpng/libpng" "${NXDK_DIR}/lib/libpng"
                      "${NXDK_DIR}/lib/zlib/zlib" "${NXDK_DIR}/lib/zlib")
elseif(EMSCRIPTEN)
  # Nothing is found on the build machine: SDL2, libpng and zlib all come from
  # Emscripten's own ports, named above and again at link time.
  set(SDL2_INCLUDE_DIRS "")
  set(SDL2_LIBRARIES "")

  add_definitions(-DUSING_LIBPNG)
  set(PNG_FOUND TRUE)
  set(PNG_LIBRARIES "")

  # WebGL is provided by the browser and bound by Emscripten itself, so there is
  # no loader to link and no GLEW to find.
  set(OPENGL_LIBRARIES "")
elseif(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Windows")
  if(NOT USE_SDL2_CONFIG)
    find_package(SDL2 REQUIRED)
  endif()
  if(USE_FREETYPE_LIBRARY)
    find_package(Freetype REQUIRED)
  endif()
  find_package(PNG)
  if(PNG_FOUND)
    add_definitions(-DUSING_LIBPNG)
  endif()
  if(USE_OPEN_ASSET_IMPORT_LIBRARY)
    find_package(assimp)
    if(assimp_FOUND)
      add_definitions(-DUSING_ASSIMP)
      set(ASSIMP_INCLUDE_DIRS assimp_INCLUDE_DIRS)
      set(ASSIMP_LIBRARIES assimp)
    endif()
  endif()
  if(USING_OPENGL)
    if(APPLE)
      # macOS has OpenGL as a framework and needs no loader.
      find_library(OPENGL_FRAMEWORK OpenGL)
      set(OPENGL_LIBRARIES ${OPENGL_FRAMEWORK})
    elseif(GLEW_LIBRARY)
      set(OPENGL_LIBRARIES "${GLEW_LIBRARY};-lGL")
    else()
      set(OPENGL_LIBRARIES "-lGL -lGLEW")
    endif()
  endif()
else()
  if(NOT USE_SDL2_CONFIG)
    set(SDL2_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/meta/win/include/SDL2/")
  endif()
  if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    if(USING_OPENGL)
      if(GLEW_LIBRARY)
        set(OPENGL_LIBRARIES "${GLEW_LIBRARY}")
      else()
        set(OPENGL_LIBRARIES "${CMAKE_CURRENT_LIST_DIR}/meta/win/lib/msvc/x64/glew32s.lib")
      endif()
    endif()
  else()
    if(USE_OPEN_ASSET_IMPORT_LIBRARY)
      set(ASSIMP_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/meta/win/include/assimp/")
      set(ASSIMP_LIBRARIES "${CMAKE_CURRENT_LIST_DIR}/meta/win/lib/mingw/${TOOLCHAIN_PREFIX}/libassimp.a;${CMAKE_CURRENT_LIST_DIR}/meta/win/lib/mingw/${TOOLCHAIN_PREFIX}/libzlibstatic.a")
    endif()
    if(USING_OPENGL)
      set(OPENGL_LIBRARIES "${CMAKE_CURRENT_LIST_DIR}/meta/win/lib/mingw/${TOOLCHAIN_PREFIX}/libglew32.a;-lgdi32;-lopengl32")
    endif()
  endif()
endif()

# Add needed directories
include_directories(
  include
  source
  ${SDL2_INCLUDE_DIRS}
  ${ASSIMP_INCLUDE_DIRS}
)

if(GLEW_INCLUDE_DIR)
  include_directories(${GLEW_INCLUDE_DIR})
  add_definitions(-DGLEW_STATIC)
endif()

if(USE_OPEN_ASSET_IMPORT_LIBRARY)
  include_directories(${ASSIMP_INCLUDE_DIRS})
endif()

# Include libraries
if(NXDK)
  # nxdk supplies everything, so none of the bundled Windows libraries apply.
elseif(EMSCRIPTEN)
  # Ports supply everything, so none of the bundled libraries apply either.
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
  # Add all of the Windows include files
  include_directories(
    meta/win/include
  )
  if(WINDOWS_USE_RESOURCE_FILE)
    target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/${WIN_RES_FILE})
  endif()
else()
  if(USE_FREETYPE_LIBRARY)
    include_directories(
      ${FREETYPE_INCLUDE_DIRS}
    )
  endif()
endif()

# Add libraries
if(NXDK)
  target_link_libraries(${PROJECT_NAME} ${SDL2_LIBRARIES})
elseif(EMSCRIPTEN)
  # Handled by the link options below, which is where Emscripten wants its
  # ports and its settings named.
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
  if(USE_SDL2_CONFIG)
    target_link_libraries(${PROJECT_NAME} ${SDL2_LIBRARIES} Ws2_32.lib opengl32.lib)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    target_link_libraries(${PROJECT_NAME} "${CMAKE_CURRENT_LIST_DIR}/meta/win/lib/msvc/x64/SDL2.lib"
    Ws2_32.lib opengl32.lib)
  else()
    target_link_libraries(${PROJECT_NAME}
      "${CMAKE_CURRENT_LIST_DIR}/meta/win/lib/mingw/${TOOLCHAIN_PREFIX}/libSDL2.a"
      -static -mwindows -lmingw32 -lm -ldinput8 -ldxguid -ldxerr8
      -luser32 -lwinmm -limm32 -lole32 -loleaut32 -lshell32
      -lversion -luuid -lhid -lsetupapi -lws2_32)
  endif()
  add_definitions(-DSDL_MAIN_HANDLED)
else()
  target_link_libraries(${PROJECT_NAME} ${SDL2_LIBRARIES})
endif()

if(USE_FREETYPE_LIBRARY)
  target_link_libraries(${PROJECT_NAME} ${FREETYPE_LIBRARIES})
endif()
if(PNG_FOUND)
  target_link_libraries(${PROJECT_NAME} ${PNG_LIBRARIES})
endif()
if(USE_OPEN_ASSET_IMPORT_LIBRARY)
  target_link_libraries(${PROJECT_NAME} ${ASSIMP_LIBRARIES})
endif()
if(USING_OPENGL)
  target_link_libraries(${PROJECT_NAME} ${OPENGL_LIBRARIES})
endif()

if(APPLE)
  # Foundation for the Objective-C filesystem code above.
  find_library(FOUNDATION_FRAMEWORK Foundation)
  target_link_libraries(${PROJECT_NAME} ${FOUNDATION_FRAMEWORK})
endif()

# The browser build is linked by emcc, which takes the shape of the finished
# page as link options rather than as libraries.
if(EMSCRIPTEN)
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

    --shell-file "${CMAKE_SOURCE_DIR}/meta/web/shell.html"
  )

  # The page is an input to the link, but naming it in a link option does not
  # tell CMake that, so editing it would otherwise leave the built page stale.
  set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY
    LINK_DEPENDS "${CMAKE_SOURCE_DIR}/meta/web/shell.html")

  if(HATCH_WEB_PRELOAD)
    target_link_options(${PROJECT_NAME} PRIVATE
      "--preload-file" "${HATCH_WEB_PRELOAD}@/")
  endif()
endif()

# What the linker produces for the Xbox is a Windows executable, which the
# console does not boot. cxbe relinks it into the XBE the Xbox does, named the
# way a disc or a dashboard expects to find it.
if(NXDK)
  set(CXBE "${NXDK_DIR}/tools/cxbe/cxbe")

  # cxbe is a host tool, and nxdk builds it on demand rather than shipping it,
  # so a checkout that has only ever had its libraries built will not have one
  # yet. Building it here keeps the Xbox target working from a plain checkout.
  if(NOT EXISTS "${CXBE}")
    # nxdk's makefile sets CXX to its own cross-compiler, which is a
    # freestanding, no-exception i386 target that cxbe is not written for and
    # that this machine could not run the result of anyway. The host compiler
    # has to be passed on the command line to override that -- the environment
    # would lose to the makefile's own assignment.
    find_program(HOST_CXX NAMES c++ g++ clang++)

    if(NOT HOST_CXX)
      message(FATAL_ERROR
        "No host C++ compiler was found to build nxdk's cxbe with. Run "
        "'make cxbe' in ${NXDK_DIR} and configure again.")
    endif()

    message(STATUS "Building nxdk's cxbe with ${HOST_CXX}")
    execute_process(
      COMMAND ${CMAKE_MAKE_PROGRAM} -C "${NXDK_DIR}/tools/cxbe" "CXX=${HOST_CXX}"
      RESULT_VARIABLE CXBE_RESULT
      OUTPUT_QUIET)

    if(NOT CXBE_RESULT EQUAL 0 OR NOT EXISTS "${CXBE}")
      message(FATAL_ERROR
        "cxbe could not be built. Run 'make cxbe' in ${NXDK_DIR} and configure again.")
    endif()
  endif()

  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND "${CXBE}"
            -OUT:"$<TARGET_FILE_DIR:${PROJECT_NAME}>/default.xbe"
            -TITLE:"${HATCH_TARGET_NAME}"
            "$<TARGET_FILE:${PROJECT_NAME}>"
    COMMENT "Relinking into default.xbe")
endif()
