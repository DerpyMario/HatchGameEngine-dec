# The original Xbox, through nxdk, the open-source toolchain for it. NXDK is set
# by nxdk's toolchain file, which calls the system Generic; CMakeLists.txt picks
# this file by asking for NXDK instead.

# The Xbox reaches its GPU through nxdk's pbkit, so the OpenGL renderer has
# nothing to talk to and its headers are not there to be found. The engine
# falls back to what it has for machines without OpenGL: the SDL2 backend over
# its own software rasteriser.
list(FILTER HATCH_SOURCES EXCLUDE REGEX "source/Engine/Rendering/GL/")
set(USING_OPENGL OFF)

# The triangulation library reports degenerate input by throwing, and nxdk has
# no exceptions to throw. Geometry::Triangulate says so and returns nothing.
list(FILTER HATCH_SOURCES EXCLUDE REGEX "source/Libraries/poly2tri/")

# Far too much for the machine, and the scripts are compiled ahead of time on a
# PC in any case.
set(USE_OPEN_ASSET_IMPORT_LIBRARY OFF)

add_executable(${PROJECT_NAME} ${HATCH_SOURCES})

# SDL_MAIN_HANDLED is not set here: nxdk's SDL defines it itself, and setting
# it again only earns a redefinition warning in every translation unit.
target_compile_definitions(${PROJECT_NAME} PRIVATE -DXBOX -DCONSOLE_FILESYSTEM)

# See the header for why this is forced into everything.
target_compile_options(${PROJECT_NAME} PRIVATE
  -include "${CMAKE_SOURCE_DIR}/source/Engine/Includes/XboxCompat.h")

set(SDL2_INCLUDE_DIRS "${NXDK_DIR}/lib/sdl/SDL2/include")
set(SDL2_LIBRARIES "${NXDK_DIR}/lib/libSDL2.lib")

set(PNG_FOUND TRUE)
set(PNG_LIBRARIES "${NXDK_DIR}/lib/libpng.lib" "${NXDK_DIR}/lib/libzlib.lib")
# nxdk keeps libpng's generated configuration a level above the sources.
target_include_directories(${PROJECT_NAME} PRIVATE
  "${NXDK_DIR}/lib/libpng/libpng" "${NXDK_DIR}/lib/libpng"
  "${NXDK_DIR}/lib/zlib/zlib" "${NXDK_DIR}/lib/zlib")

# What the linker produces for the Xbox is a Windows executable, which the
# console does not boot. cxbe relinks it into the XBE the Xbox does, named the
# way a disc or a dashboard expects to find it.
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
