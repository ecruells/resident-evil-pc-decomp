set(PS2SDK "$ENV{PS2SDK}")
set(GSKIT "$ENV{GSKIT}")
if(NOT GSKIT)
    set(GSKIT "$ENV{PS2DEV}/gsKit")
endif()
find_package(SDL2 CONFIG REQUIRED PATHS "${PS2SDK}/ports/lib/cmake/SDL2" NO_DEFAULT_PATH)
add_executable(residentevil ${RE1_SOURCES}
    src/platform/linux/input.cpp src/platform/linux/audio.cpp
    src/platform/ps2/platform.cpp src/platform/ps2/task_context.cpp
    src/platform/ps2/main.cpp src/platform/ps2/video.cpp
    src/marni/MarniDX_SDL.cpp)
set_target_properties(residentevil PROPERTIES SUFFIX ".elf")
target_compile_definitions(residentevil PRIVATE RE1_PS2 _EE __PS2__)
target_include_directories(residentevil PRIVATE src
    "${PS2SDK}/ee/include" "${PS2SDK}/common/include" "${PS2SDK}/ports/include")
target_compile_options(residentevil PRIVATE -G0 -O2 -g -Wall -Wextra
    -Wno-unused-parameter -Wno-missing-field-initializers
    -fno-strict-aliasing -fno-strict-overflow -ffunction-sections -fdata-sections)
target_link_directories(residentevil PRIVATE "${PS2SDK}/ee/lib" "${PS2SDK}/ports/lib" "${GSKIT}/lib")
target_link_options(residentevil PRIVATE "-T${PS2SDK}/ee/startup/linkfile"
    -Wl,-zmax-page-size=128 -Wl,--gc-sections "-Wl,-Map=${CMAKE_BINARY_DIR}/residentevil.map")
target_link_libraries(residentevil PRIVATE SDL2::SDL2main SDL2::SDL2-static)
