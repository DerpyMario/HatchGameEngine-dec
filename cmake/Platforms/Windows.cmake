add_executable(${PROJECT_NAME} ${HATCH_SOURCES})
target_compile_definitions(${PROJECT_NAME} PRIVATE -DWIN32 -D_WINDOWS)

option(WINDOWS_USE_RESOURCE_FILE "Use resource file (Windows)" ON)

if(${CMAKE_BUILD_TYPE} MATCHES "Debug")
  option(WINDOWS_COMPILE_AS_CONSOLE_APP "Compile as a console application" ON)
else()
  option(WINDOWS_COMPILE_AS_CONSOLE_APP "Compile as a console application" OFF)
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4244 /wd4267 /wd4305 /wd4717 /EHsc")

option(USING_DIRECT3D "Use Direct3D" OFF)

if(USING_DIRECT3D)
  target_compile_definitions(${PROJECT_NAME} PRIVATE -DUSING_DIRECT3D)
endif()

set(WIN_RES_FILE "${CMAKE_SOURCE_DIR}/meta/win/HatchGameEngine.rc")

if(RESOURCE_FILE)
  set(WIN_RES_FILE ${RESOURCE_FILE})
endif()

# A GLEW library that was already built can be named instead of fetching one,
# which is how the release workflow builds it against the static C runtime.
set(GLEW_LIBRARY "" CACHE FILEPATH "GLEW library to link instead of fetching one")
set(GLEW_INCLUDE_DIR "" CACHE PATH "Where that GLEW's headers are")

if(USING_OPENGL)
  if(GLEW_LIBRARY)
    set(OPENGL_LIBRARIES "${GLEW_LIBRARY}")
    target_compile_definitions(${PROJECT_NAME} PRIVATE -DGLEW_STATIC)
    target_include_directories(${PROJECT_NAME} PRIVATE ${GLEW_INCLUDE_DIR})
  else()
    include(cmake/Dependencies/FetchGLEW.cmake)
  endif()
endif()

if(WINDOWS_USE_RESOURCE_FILE)
  target_sources(${PROJECT_NAME} PRIVATE ${WIN_RES_FILE})
endif()

# The same for SDL2, found through the CMake package an SDL2 build installs.
option(USE_SDL2_CONFIG "Find SDL2 through its own CMake package instead of fetching it" OFF)

if(USE_SDL2_CONFIG)
  find_package(SDL2 CONFIG REQUIRED)

  if(TARGET SDL2::SDL2-static)
    set(SDL2_LIBRARIES SDL2::SDL2-static)
  else()
    set(SDL2_LIBRARIES SDL2::SDL2)
  endif()

  set(SDL2_INCLUDE_DIRS "")
else()
  include(cmake/Dependencies/FetchSDL2.cmake)
endif()

if(USE_OPEN_ASSET_IMPORT_LIBRARY)
  include(cmake/Dependencies/Fetchassimp.cmake)
endif()

target_link_libraries(${PROJECT_NAME}
  Ws2_32.lib opengl32.lib winmm.lib imm32.lib version.lib setupapi.lib)

if(NOT WINDOWS_COMPILE_AS_CONSOLE_APP)
  set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE ON)
  target_link_options(${PROJECT_NAME} PRIVATE "LINKER:/ENTRY:mainCRTStartup")
else()
  target_compile_definitions(${PROJECT_NAME} PRIVATE -DWINDOWS_CONSOLE_APP)
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE -DSDL_MAIN_HANDLED)
