add_executable(${PROJECT_NAME} ${HATCH_SOURCES})
target_compile_definitions(${PROJECT_NAME} PRIVATE -DLINUX)

# Builds against an SDL2 that was built from source, through the CMake package
# SDL2 installs, which knows every library its static build needs. This is how
# the release workflow gets an executable that carries SDL2 with it instead of
# asking for it to be installed.
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

# The same idea for GLEW: a static one named here is linked instead of the
# system's shared library.
set(GLEW_LIBRARY "" CACHE FILEPATH "GLEW library to link instead of the system one")
set(GLEW_INCLUDE_DIR "" CACHE PATH "Where that GLEW's headers are")

if(USING_OPENGL)
  if(GLEW_LIBRARY)
    set(OPENGL_LIBRARIES "${GLEW_LIBRARY};-lGL")
    target_compile_definitions(${PROJECT_NAME} PRIVATE -DGLEW_STATIC)
    if(GLEW_INCLUDE_DIR)
      target_include_directories(${PROJECT_NAME} PRIVATE ${GLEW_INCLUDE_DIR})
    endif()
  else()
    set(OPENGL_LIBRARIES "-lGL -lGLEW")
  endif()
endif()
