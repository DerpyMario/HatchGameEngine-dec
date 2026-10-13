add_executable(${PROJECT_NAME} ${HATCH_SOURCES})
target_compile_definitions(${PROJECT_NAME} PRIVATE -DMACOSX)

# The macOS half of the filesystem code is Objective-C, since the application
# support directory can only be asked for through Foundation.
enable_language(OBJC)
file(GLOB_RECURSE OBJC_SOURCES RELATIVE ${CMAKE_SOURCE_DIR} "source/Engine/*.m")
target_sources(${PROJECT_NAME} PRIVATE ${OBJC_SOURCES})

# See Linux.cmake: a static SDL2 found through its own CMake package, which is
# how the release workflow builds one application for both architectures.
option(USE_SDL2_CONFIG "Find SDL2 through its own CMake package" OFF)

if(USE_SDL2_CONFIG)
  find_package(SDL2 CONFIG REQUIRED)

  if(TARGET SDL2::SDL2-static)
    set(SDL2_LIBRARIES SDL2::SDL2-static)
  else()
    set(SDL2_LIBRARIES SDL2::SDL2)
  endif()

  set(SDL2_INCLUDE_DIRS "")
else()
  find_package(SDL2 REQUIRED)
endif()

find_package(PNG)

if(USE_OPEN_ASSET_IMPORT_LIBRARY)
  find_package(assimp)
  if(assimp_FOUND)
    set(ASSIMP_INCLUDE_DIRS "${assimp_INCLUDE_DIRS}")
    set(ASSIMP_LIBRARIES "${assimp_LIBRARIES}")
  endif()
endif()

# macOS has OpenGL as a framework and needs no loader.
if(USING_OPENGL)
  find_library(OPENGL_FRAMEWORK OpenGL)
  set(OPENGL_LIBRARIES ${OPENGL_FRAMEWORK})
endif()

find_library(FOUNDATION_FRAMEWORK Foundation)
target_link_libraries(${PROJECT_NAME} ${FOUNDATION_FRAMEWORK})
