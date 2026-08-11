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
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64|i[3-6]86)$" AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -march=nocona")
  endif()
endif()

# Set up CMake modules path
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")

file(GLOB_RECURSE C_SOURCES RELATIVE ${CMAKE_SOURCE_DIR} "source/*.c")
file(GLOB_RECURSE CPP_SOURCES RELATIVE ${CMAKE_SOURCE_DIR} "source/*.cpp")

set(HATCH_SOURCES ${C_SOURCES} ${CPP_SOURCES})

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

if(APPLE)
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
if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Windows")
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
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
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
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
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
