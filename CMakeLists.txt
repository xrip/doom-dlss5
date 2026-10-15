cmake_minimum_required(VERSION 3.28)
project(doom_raylib VERSION 0.1 LANGUAGES C)

if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "Use Clang: cmake --preset clang")
endif()
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(DOOM_DLSS5 "Build the DLSS 5 neural-rendering adapter" OFF)
option(DOOM_FRAME_GENERATION "Build direct NGX Vulkan frame generation" OFF)
if(DOOM_FRAME_GENERATION AND NOT DOOM_DLSS5)
    message(FATAL_ERROR "Frame generation uses the NGX bridge; enable DOOM_DLSS5")
endif()
set(DLSS5_SDK "" CACHE PATH "NVIDIA public NGX SDK root")
set(DLSS5_RUNTIME "" CACHE FILEPATH "Path to the matching nvngx_dlssnr.dll runtime")
if(DOOM_DLSS5)
    if(NOT EXISTS "${DLSS5_SDK}/include/nvsdk_ngx_vk.h")
        message(FATAL_ERROR "Set DLSS5_SDK to the public NVIDIA/DLSS checkout containing include/nvsdk_ngx_vk.h")
    endif()
    if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "The DLSS 5 adapter currently targets Windows x64")
    endif()
    if(NOT EXISTS "${DLSS5_RUNTIME}")
        message(FATAL_ERROR "Set DLSS5_RUNTIME to the matching nvngx_dlssnr.dll; it is separate from the public SDK")
    endif()
endif()

include(FetchContent)
find_package(Vulkan 1.3 REQUIRED COMPONENTS glslc)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG a74efa0d5628b74adc0426af4c5710e287fa7c2c)
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20)
FetchContent_MakeAvailable(glfw stb)
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
add_executable(wad-convert src/convert.c src/voxel_convert.c)
target_link_libraries(wad-convert PRIVATE scene tess2 mus)
add_library(lighting STATIC src/lighting.c)
target_link_libraries(lighting PUBLIC scene)
add_library(renderer STATIC src/renderer.c)
target_link_libraries(renderer PUBLIC movement lighting glfw Vulkan::Vulkan)
target_include_directories(renderer SYSTEM PRIVATE ${stb_SOURCE_DIR})
target_compile_definitions(renderer PUBLIC GLFW_INCLUDE_NONE)
target_include_directories(renderer PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/shaders)
add_executable(doom-raylib src/main.c src/level_prepare.c)
add_dependencies(doom-raylib wad-convert)
target_link_libraries(doom-raylib PRIVATE renderer music)
if(DOOM_DLSS5 OR DOOM_FRAME_GENERATION)
    if(NOT DEFINED CMAKE_CXX_COMPILER)
        set(CMAKE_CXX_COMPILER clang++ CACHE STRING "Clang C++ compiler for the NGX bridge")
    endif()
    enable_language(CXX)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "Use Clang++ for the NGX bridge")
    endif()
endif()
if(DOOM_DLSS5)
    find_library(DLSS5_NGX_LIBRARY NAMES nvsdk_ngx_d
        PATHS "${DLSS5_SDK}/lib/Windows_x86_64/x64" NO_DEFAULT_PATH REQUIRED)
    find_library(DLSS5_NGX_DEBUG_LIBRARY NAMES nvsdk_ngx_d_dbg
        PATHS "${DLSS5_SDK}/lib/Windows_x86_64/x64" NO_DEFAULT_PATH REQUIRED)
    find_file(DLSS5_SR_RUNTIME NAMES nvngx_dlss.dll
        PATHS "${DLSS5_SDK}/lib/Windows_x86_64/rel" NO_DEFAULT_PATH REQUIRED)
    find_file(DLSS5_RR_RUNTIME NAMES nvngx_dlssd.dll
        PATHS "${DLSS5_SDK}/lib/Windows_x86_64/rel" NO_DEFAULT_PATH REQUIRED)
    add_library(dlss5 STATIC src/dlss5.cpp)
    target_compile_features(dlss5 PRIVATE cxx_std_17)
    target_include_directories(dlss5 SYSTEM PRIVATE "${DLSS5_SDK}/include")
    target_compile_definitions(dlss5 PRIVATE DOOM_DLSS5 _CRT_SECURE_NO_WARNINGS)
    target_compile_options(dlss5 PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    target_link_libraries(dlss5 PUBLIC Vulkan::Vulkan
        optimized ${DLSS5_NGX_LIBRARY} debug ${DLSS5_NGX_DEBUG_LIBRARY})
    target_link_libraries(renderer PRIVATE dlss5)
    target_compile_definitions(renderer PUBLIC DOOM_DLSS5)
    function(copy_dlss_runtime target directory)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${directory}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLSS5_RUNTIME}" "${directory}/nvngx_dlssnr.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLSS5_SR_RUNTIME}" "${directory}/nvngx_dlss.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLSS5_RR_RUNTIME}" "${directory}/nvngx_dlssd.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:${target}> "${directory}/nvngx.dll"
            VERBATIM)
    endfunction()
    copy_dlss_runtime(doom-raylib "$<TARGET_FILE_DIR:doom-raylib>")
endif()
if(DOOM_FRAME_GENERATION)
    find_file(DLSS5_FG_RUNTIME NAMES nvngx_dlssg.dll
        PATHS "${DLSS5_SDK}/lib/Windows_x86_64/rel" NO_DEFAULT_PATH REQUIRED)
    add_library(frame-generation STATIC src/frame_generation.cpp src/presentation.c)
    target_compile_features(frame-generation PRIVATE cxx_std_17)
    target_include_directories(frame-generation SYSTEM PRIVATE "${DLSS5_SDK}/include")
    target_compile_definitions(frame-generation PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(frame-generation PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    target_link_libraries(frame-generation PUBLIC dlss5)
    target_link_libraries(renderer PRIVATE frame-generation)
    target_compile_definitions(renderer PUBLIC DOOM_FRAME_GENERATION)
    function(copy_fg_runtime target directory)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${directory}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLSS5_FG_RUNTIME}"
                "${directory}/nvngx_dlssg.dll"
            VERBATIM)
    endfunction()
    copy_fg_runtime(doom-raylib "$<TARGET_FILE_DIR:doom-raylib>")
endif()
set(render_shader_outputs)
foreach(shader vulkan.vert vulkan.frag hud.vert hud.frag resolve.comp depth.vert depth.frag)
    list(APPEND render_shader_outputs ${CMAKE_CURRENT_BINARY_DIR}/shaders/${shader}.inc)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shaders/${shader}.inc
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders
        COMMAND Vulkan::glslc --target-env=vulkan1.3 -mfmt=num
            ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/${shader}
            -o ${CMAKE_CURRENT_BINARY_DIR}/shaders/${shader}.inc
        DEPENDS src/shaders/${shader} src/shaders/color.glsl src/shaders/objects.glsl
        VERBATIM)
endforeach()
list(APPEND render_shader_outputs ${CMAKE_CURRENT_BINARY_DIR}/shaders/depth-merge.frag.inc)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shaders/depth-merge.frag.inc
    COMMAND Vulkan::glslc --target-env=vulkan1.3 -DSPRITE_DEPTH_MERGE -mfmt=num
        ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/depth.frag
        -o ${CMAKE_CURRENT_BINARY_DIR}/shaders/depth-merge.frag.inc
    DEPENDS src/shaders/depth.frag
    VERBATIM)
foreach(variant IN ITEMS ray reconstruction)
    set(definitions -DRAY_LIGHTING)
    if(variant STREQUAL "reconstruction")
        list(APPEND definitions -DRAY_RECONSTRUCTION)
    endif()
    set(output ${CMAKE_CURRENT_BINARY_DIR}/shaders/vulkan-${variant}.frag.inc)
    list(APPEND render_shader_outputs ${output})
    add_custom_command(OUTPUT ${output}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders
        COMMAND Vulkan::glslc --target-env=vulkan1.3 ${definitions} -mfmt=num
            ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/vulkan.frag -o ${output}
        DEPENDS src/shaders/vulkan.frag src/shaders/lighting.glsl src/shaders/color.glsl src/shaders/objects.glsl
        VERBATIM)
endforeach()
add_custom_target(vulkan-shaders DEPENDS ${render_shader_outputs})
add_dependencies(renderer vulkan-shaders)
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
add_executable(check-lighting tests/check_lighting.c)
target_link_libraries(check-lighting PRIVATE lighting)
add_executable(check-voxel-state tests/check_voxel_state.c)
target_link_libraries(check-voxel-state PRIVATE scene)
foreach(target scene movement lighting mus music renderer mus-convert check-music wad-convert doom-raylib check-movement check-doors check-floors check-specials check-movers check-lighting check-voxel-state)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if(WIN32)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
endforeach()
if(NOT WIN32)
    target_link_libraries(scene PUBLIC m)
endif()

enable_testing()
set(test_scene ${CMAKE_CURRENT_BINARY_DIR}/cache/tests/MAP01.drmap)
set(test_music ${CMAKE_CURRENT_BINARY_DIR}/cache/tests/MAP01.mid)
add_test(NAME natural-lighting COMMAND check-lighting)
add_test(NAME voxel-state COMMAND check-voxel-state)
option(DOOM_GPU_TESTS "Run Vulkan GPU tests (requires a GPU, validation layer, and local DOOM2.WAD)" OFF)
if(DOOM_GPU_TESTS)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/DOOM2.WAD")
        message(FATAL_ERROR "DOOM_GPU_TESTS requires local DOOM2.WAD")
    endif()
    add_executable(check-renderer tests/check_renderer.c)
    target_link_libraries(check-renderer PRIVATE renderer)
    if(DOOM_FRAME_GENERATION)
        copy_fg_runtime(check-renderer "$<TARGET_FILE_DIR:check-renderer>")
    endif()
    target_compile_options(check-renderer PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if(WIN32)
        target_compile_definitions(check-renderer PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME vulkan-renderer COMMAND check-renderer ${test_scene})
    add_test(NAME vulkan-ray-lighting COMMAND check-renderer ${test_scene} --ray-lighting)
    add_test(NAME vulkan-no-ngx COMMAND doom-raylib ${test_scene} --no-ngx --frames 2 --validation --no-music)
    set_tests_properties(vulkan-no-ngx PROPERTIES ENVIRONMENT "VK_VALIDATION_VALIDATE_SYNC=1"
        PASS_REGULAR_EXPRESSION "NGX disabled for this process"
        FAIL_REGULAR_EXPRESSION "DLSS 5 neural rendering evaluation recorded|DLSS Ray Reconstruction evaluation recorded|DLSS Super Resolution evaluation recorded")
    set_tests_properties(vulkan-ray-lighting PROPERTIES ENVIRONMENT "VK_VALIDATION_VALIDATE_SYNC=1")
    set_tests_properties(vulkan-renderer PROPERTIES ENVIRONMENT
        "VK_VALIDATION_VALIDATE_SYNC=1")
    if(DOOM_DLSS5)
        copy_dlss_runtime(check-renderer "$<TARGET_FILE_DIR:check-renderer>/nr-check")
        add_test(NAME vulkan-default-mode COMMAND doom-raylib
            ${test_scene} --frames 2 --validation --no-music)
        set_tests_properties(vulkan-default-mode PROPERTIES
            ENVIRONMENT "VK_VALIDATION_VALIDATE_SYNC=1"
            PASS_REGULAR_EXPRESSION "DLSS 5 neural rendering evaluation recorded"
            FAIL_REGULAR_EXPRESSION "DLSS Ray Reconstruction evaluation recorded")
        add_test(NAME vulkan-dlss5 COMMAND $<TARGET_FILE_DIR:check-renderer>/nr-check/nvngx.dll
            ${test_scene} --dlss5)
        add_test(NAME vulkan-reconstruction COMMAND $<TARGET_FILE_DIR:check-renderer>/nr-check/nvngx.dll
            ${test_scene} --dlss5 --ray-lighting --rr)
        set_tests_properties(vulkan-reconstruction PROPERTIES ENVIRONMENT "VK_VALIDATION_VALIDATE_SYNC=1")
        set_tests_properties(vulkan-default-mode vulkan-dlss5 vulkan-reconstruction PROPERTIES LABELS ngx)
        if(DOOM_FRAME_GENERATION)
            copy_fg_runtime(check-renderer "$<TARGET_FILE_DIR:check-renderer>/nr-check")
            add_test(NAME vulkan-frame-generation COMMAND $<TARGET_FILE_DIR:check-renderer>/nr-check/nvngx.dll
                ${test_scene} --dlss5 --fg)
            set_tests_properties(vulkan-frame-generation PROPERTIES ENVIRONMENT "VK_VALIDATION_VALIDATE_SYNC=1")
            add_test(NAME vulkan-ray-neural COMMAND $<TARGET_FILE_DIR:check-renderer>/nr-check/nvngx.dll
                ${test_scene} --dlss5 --fg --ray-lighting)
            set_tests_properties(vulkan-ray-neural PROPERTIES ENVIRONMENT "VK_VALIDATION_VALIDATE_SYNC=1")
            add_test(NAME vulkan-reconstruction-fg COMMAND $<TARGET_FILE_DIR:check-renderer>/nr-check/nvngx.dll
                ${test_scene} --dlss5 --fg --ray-lighting --rr)
            set_tests_properties(vulkan-reconstruction-fg PROPERTIES ENVIRONMENT "VK_VALIDATION_VALIDATE_SYNC=1")
            set_tests_properties(vulkan-frame-generation vulkan-ray-neural vulkan-reconstruction-fg PROPERTIES LABELS ngx)
        endif()
        set_tests_properties(vulkan-dlss5 PROPERTIES ENVIRONMENT
            "VK_VALIDATION_VALIDATE_SYNC=1")
    endif()
endif()
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
    add_test(NAME voxel-conversion COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_voxels.py $<TARGET_FILE:wad-convert> $<TARGET_FILE:doom-raylib>)
    add_test(NAME wad-preparation COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_prepare.py $<TARGET_FILE:wad-convert> $<TARGET_FILE:doom-raylib>)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/DOOM2.WAD")
    add_custom_command(OUTPUT ${test_scene} ${test_music}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/cache/tests
        COMMAND wad-convert ${CMAKE_CURRENT_SOURCE_DIR}/DOOM2.WAD ${test_scene}
        DEPENDS wad-convert ${CMAKE_CURRENT_SOURCE_DIR}/DOOM2.WAD
        VERBATIM)
    add_custom_target(map01 ALL DEPENDS ${test_scene})
    add_test(NAME map01-scene COMMAND doom-raylib
        ${test_scene} --inspect)
    add_test(NAME map01-movement COMMAND check-movement ${test_scene})
    add_test(NAME map01-doors COMMAND check-doors ${test_scene})
    add_test(NAME map01-floors COMMAND check-floors ${test_scene})
    add_test(NAME map01-interaction COMMAND check-specials ${test_scene})
    if(Python3_FOUND)
        add_test(NAME doom2-all-levels COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_levels.py $<TARGET_FILE:wad-convert>
            $<TARGET_FILE:doom-raylib> ${CMAKE_CURRENT_SOURCE_DIR}/DOOM2.WAD $<TARGET_FILE:check-specials>)
        if(DOOM_GPU_TESTS)
            add_test(NAME doom2-vulkan-levels COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_levels.py $<TARGET_FILE:wad-convert>
                $<TARGET_FILE:doom-raylib> ${CMAKE_CURRENT_SOURCE_DIR}/DOOM2.WAD
                $<TARGET_FILE:check-specials> --render)
            set_tests_properties(doom2-vulkan-levels PROPERTIES ENVIRONMENT
                "VK_VALIDATION_VALIDATE_SYNC=1")
            if(DOOM_DLSS5)
                add_test(NAME doom2-neural-levels COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_levels.py $<TARGET_FILE:wad-convert>
                    $<TARGET_FILE:doom-raylib> ${CMAKE_CURRENT_SOURCE_DIR}/DOOM2.WAD
                    $<TARGET_FILE:check-specials> --render --dlss5)
                set_tests_properties(doom2-neural-levels PROPERTIES ENVIRONMENT
                    "VK_VALIDATION_VALIDATE_SYNC=1" LABELS ngx)
                if(DOOM_FRAME_GENERATION)
                    add_test(NAME doom2-frame-generation-levels COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_levels.py $<TARGET_FILE:wad-convert>
                        $<TARGET_FILE:doom-raylib> ${CMAKE_CURRENT_SOURCE_DIR}/DOOM2.WAD
                        $<TARGET_FILE:check-specials> --render --dlss5 --fg)
                    set_tests_properties(doom2-frame-generation-levels PROPERTIES ENVIRONMENT
                        "VK_VALIDATION_VALIDATE_SYNC=1" LABELS ngx)
                    add_test(NAME doom2-ray-neural-levels COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_levels.py $<TARGET_FILE:wad-convert>
                        $<TARGET_FILE:doom-raylib> ${CMAKE_CURRENT_SOURCE_DIR}/DOOM2.WAD
                        $<TARGET_FILE:check-specials> --render --dlss5 --fg --ray-lighting)
                    set_tests_properties(doom2-ray-neural-levels PROPERTIES ENVIRONMENT
                        "VK_VALIDATION_VALIDATE_SYNC=1" LABELS ngx)
                    add_test(NAME doom2-reconstruction-levels COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_levels.py $<TARGET_FILE:wad-convert>
                        $<TARGET_FILE:doom-raylib> ${CMAKE_CURRENT_SOURCE_DIR}/DOOM2.WAD
                        $<TARGET_FILE:check-specials> --render --dlss5 --fg --ray-lighting --rr)
                    set_tests_properties(doom2-reconstruction-levels PROPERTIES ENVIRONMENT
                        "VK_VALIDATION_VALIDATE_SYNC=1" LABELS ngx)
                endif()
            endif()
        endif()
    endif()
endif()
