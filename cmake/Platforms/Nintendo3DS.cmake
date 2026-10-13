# The Nintendo 3DS, through devkitPro: devkitARM, libctru, and SDL2's own 3DS
# port. devkitPro's 3DS toolchain file, $DEVKITPRO/cmake/3DS.cmake, names the
# system Nintendo3DS, which is what brings the build here.
#
# devkitPro packages SDL 1.2 for the 3DS but not SDL2, so SDL2 has to be built
# from source with that same toolchain file and installed into
# $DEVKITPRO/portlibs/3ds first. The README has the commands.
#
# The 3DS has no OpenGL: SDL2 draws in software there, so the engine uses its
# SDL2 renderer.

add_executable(${PROJECT_NAME} ${HATCH_SOURCES})

# SDL_MAIN_HANDLED because Main.cpp does what SDL2main would on the 3DS --
# turning on the New 3DS's speed-up and mounting the RomFS -- itself.
target_compile_definitions(${PROJECT_NAME} PRIVATE -DN3DS -DCONSOLE_FILESYSTEM -DSDL_MAIN_HANDLED)

# The 3DS has its CPU and memory to itself, and nothing to put a model
# converter's worth of code in.
set(USING_OPENGL OFF)
set(USE_OPEN_ASSET_IMPORT_LIBRARY OFF)

find_package(SDL2 REQUIRED CONFIG)
set(SDL2_LIBRARIES SDL2::SDL2-static)

find_package(PNG)

set(ICON_FILE "${CMAKE_SOURCE_DIR}/meta/3ds/icon.png" CACHE FILEPATH "48x48 icon for the Home Menu and the Homebrew Launcher")
set(APP_TITLE "Hatch Game Engine" CACHE STRING "Title shown in the Homebrew Launcher")
set(APP_DESCRIPTION "Runs games made with Hatch" CACHE STRING "Description shown in the Homebrew Launcher")
set(APP_AUTHOR "HatchGameEngine" CACHE STRING "Author shown in the Homebrew Launcher")

set(SMDH_FILE "${CMAKE_BINARY_DIR}/${HATCH_TARGET_NAME}.smdh")
ctr_generate_smdh(
  OUTPUT "${SMDH_FILE}"
  NAME "${APP_TITLE}"
  DESCRIPTION "${APP_DESCRIPTION}"
  AUTHOR "${APP_AUTHOR}"
  ICON "${ICON_FILE}"
)

# Like the Switch, a game's data can go into the executable as its RomFS, in
# which case the engine reads it from there; without one, the engine reads
# Data.hatch or a Resources folder from beside the .3dsx on the SD card. Which
# of the two is decided when the game starts rather than here, so that the
# engine's own --export-3ds can give an already-built .3dsx a RomFS.
set(ROMFS_PATH "" CACHE PATH "Optional path to a folder containing game data")
set(OUTPUT_3DSX "${CMAKE_BINARY_DIR}/${HATCH_TARGET_NAME}.3dsx")
if (NOT ROMFS_PATH STREQUAL "")
  message(STATUS "3DS: Building with romfs: ${ROMFS_PATH}")
  ctr_create_3dsx(${PROJECT_NAME} OUTPUT "${OUTPUT_3DSX}" SMDH "${SMDH_FILE}" ROMFS "${ROMFS_PATH}")
else()
  message(STATUS "3DS: Building without romfs")
  ctr_create_3dsx(${PROJECT_NAME} OUTPUT "${OUTPUT_3DSX}" SMDH "${SMDH_FILE}")
endif()
