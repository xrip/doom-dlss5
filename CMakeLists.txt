cmake_minimum_required(VERSION 3.28)
project(doom_raylib VERSION 0.1 LANGUAGES C)

if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "Use Clang: cmake --preset clang")
endif()
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CUSTOMIZE_BUILD ON CACHE BOOL "" FORCE)
set(SUPPORT_MODULE_RAUDIO OFF CACHE BOOL "" FORCE)
foreach(feature SUPPORT_CUSTOM_FRAME_CONTROL SUPPORT_CAMERA_SYSTEM SUPPORT_GESTURES_SYSTEM
        SUPPORT_MOUSE_GESTURES SUPPORT_AUTOMATION_EVENTS SUPPORT_COMPRESSION_API
        SUPPORT_CLIPBOARD_IMAGE SUPPORT_GPU_SKINNING SUPPORT_MESH_GENERATION
        SUPPORT_FILEFORMAT_OBJ SUPPORT_FILEFORMAT_MTL SUPPORT_FILEFORMAT_IQM
        SUPPORT_FILEFORMAT_GLTF SUPPORT_FILEFORMAT_VOX SUPPORT_FILEFORMAT_M3D
        SUPPORT_FILEFORMAT_BMP SUPPORT_FILEFORMAT_TGA SUPPORT_FILEFORMAT_JPG
        SUPPORT_FILEFORMAT_GIF SUPPORT_FILEFORMAT_QOI SUPPORT_FILEFORMAT_PSD
        SUPPORT_FILEFORMAT_DDS SUPPORT_FILEFORMAT_HDR SUPPORT_FILEFORMAT_PIC
        SUPPORT_FILEFORMAT_PNM SUPPORT_FILEFORMAT_KTX SUPPORT_FILEFORMAT_ASTC
        SUPPORT_FILEFORMAT_PKM SUPPORT_FILEFORMAT_PVR SUPPORT_FILEFORMAT_TTF
        SUPPORT_FILEFORMAT_FNT SUPPORT_FILEFORMAT_BDF SUPPORT_IMAGE_GENERATION)
    set(${feature} OFF CACHE BOOL "" FORCE)
endforeach()
set(OPENGL_VERSION 3.3 CACHE STRING "" FORCE)
FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG dbc56a87da87d973a9c5baa4e7438a9d20121d28)
FetchContent_MakeAvailable(raylib)
FetchContent_Declare(libtess2
    GIT_REPOSITORY https://github.com/memononen/libtess2.git
    GIT_TAG 8dbd6483e920311a58c9af10a10beb278efebc36)
FetchContent_MakeAvailable(libtess2)
add_library(tess2 STATIC
    ${libtess2_SOURCE_DIR}/Source/bucketalloc.c
    ${libtess2_SOURCE_DIR}/Source/dict.c
    ${libtess2_SOURCE_DIR}/Source/geom.c
    ${libtess2_SOURCE_DIR}/Source/mesh.c
    ${libtess2_SOURCE_DIR}/Source/priorityq.c
    ${libtess2_SOURCE_DIR}/Source/sweep.c
    ${libtess2_SOURCE_DIR}/Source/tess.c)
target_include_directories(tess2 PUBLIC ${libtess2_SOURCE_DIR}/Include)

add_library(scene STATIC src/scene.c src/geometry.c)
target_include_directories(scene PUBLIC src)
add_library(movement STATIC src/movement.c src/specials.c src/sector_movers.c)
target_link_libraries(movement PUBLIC scene)
add_library(mus STATIC src/mus.c)
target_include_directories(mus PUBLIC src)
add_library(music STATIC src/music.c)
target_include_directories(music PUBLIC src)
if(WIN32)
    target_link_libraries(music PRIVATE winmm)
endif()
add_executable(mus-convert src/mus_convert.c)
target_link_libraries(mus-convert PRIVATE mus scene)
add_executable(check-music tests/check_music.c)
target_link_libraries(check-music PRIVATE mus)
add_executable(wad-convert src/convert.c)
target_link_libraries(wad-convert PRIVATE scene tess2 mus)
add_executable(doom-raylib src/main.c)
target_link_libraries(doom-raylib PRIVATE movement music raylib)
add_executable(check-movement tests/check_movement.c)
target_link_libraries(check-movement PRIVATE movement)
add_executable(check-doors tests/check_doors.c)
target_link_libraries(check-doors PRIVATE movement)
add_executable(check-floors tests/check_floors.c)
target_link_libraries(check-floors PRIVATE movement)
add_executable(check-specials tests/check_specials.c)
target_link_libraries(check-specials PRIVATE movement)
add_executable(check-movers tests/check_movers.c)
target_link_libraries(check-movers PRIVATE movement)
foreach(target scene movement mus music mus-convert check-music wad-convert doom-raylib check-movement check-doors check-floors check-specials check-movers)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if(WIN32)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
endforeach()
if(NOT WIN32)
    target_link_libraries(scene PUBLIC m)
endif()

enable_testing()
add_test(NAME geometry COMMAND wad-convert --self-test)
add_test(NAME player-movement COMMAND check-movement)
add_test(NAME mus-midi COMMAND check-music)
add_test(NAME sector-doors COMMAND check-doors)
add_test(NAME sector-floors COMMAND check-floors)
add_test(NAME map-interaction COMMAND check-specials)
add_test(NAME sector-movers COMMAND check-movers)
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME wad-validation COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_wad.py $<TARGET_FILE:wad-convert> $<TARGET_FILE:doom-raylib>)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/DOOM2.WAD")
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/MAP01.drmap ${CMAKE_CURRENT_BINARY_DIR}/MAP01.mid
        COMMAND wad-convert ${CMAKE_CURRENT_SOURCE_DIR}/DOOM2.WAD ${CMAKE_CURRENT_BINARY_DIR}/MAP01.drmap
        DEPENDS wad-convert ${CMAKE_CURRENT_SOURCE_DIR}/DOOM2.WAD
        VERBATIM)
    add_custom_target(map01 ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/MAP01.drmap)
    add_test(NAME map01-scene COMMAND doom-raylib
        ${CMAKE_CURRENT_BINARY_DIR}/MAP01.drmap --inspect)
    add_test(NAME map01-movement COMMAND check-movement ${CMAKE_CURRENT_BINARY_DIR}/MAP01.drmap)
    add_test(NAME map01-doors COMMAND check-doors ${CMAKE_CURRENT_BINARY_DIR}/MAP01.drmap)
    add_test(NAME map01-floors COMMAND check-floors ${CMAKE_CURRENT_BINARY_DIR}/MAP01.drmap)
    add_test(NAME map01-interaction COMMAND check-specials ${CMAKE_CURRENT_BINARY_DIR}/MAP01.drmap)
    if(Python3_FOUND)
        add_test(NAME doom2-all-levels COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_levels.py $<TARGET_FILE:wad-convert>
            $<TARGET_FILE:doom-raylib> ${CMAKE_CURRENT_SOURCE_DIR}/DOOM2.WAD $<TARGET_FILE:check-specials>)
    endif()
endif()
