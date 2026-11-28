cmake_minimum_required(VERSION 3.24)

option(FLEET_BUILD_HOST "Build the Fleet programs, which need libultraship" ON)
option(FLEET_USE_AUTO_VCPKG "Bootstrap dependencies when no toolchain is supplied" ON)

project(Fleet LANGUAGES C CXX)

# Fleet binds both games to its own libultraship, so it must be built exactly like theirs.
if(FLEET_BUILD_HOST AND WIN32 AND FLEET_USE_AUTO_VCPKG AND NOT CMAKE_TOOLCHAIN_FILE)
    include(libultraship/cmake/automate-vcpkg.cmake)
    set(VCPKG_TRIPLET x64-windows-static-md)
    set(VCPKG_TARGET_TRIPLET x64-windows-static-md)
    vcpkg_bootstrap()
    vcpkg_install_packages(zlib bzip2 libzip libpng sdl2 sdl2-net glew glfw3 nlohmann-json tinyxml2 spdlog)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/x64/$<CONFIG>")

find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
    include(FetchContent)
    FetchContent_Declare(nlohmann_json URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz)
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# The combined randomizer engine: game-agnostic headers that talk to each game through JSON. The seed formats
# both games also read live with the Fleet contract in libultraship.
add_library(fleet-rando INTERFACE)
target_include_directories(fleet-rando INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/combo
                                                 ${CMAKE_CURRENT_SOURCE_DIR}/libultraship/include/fleet/rando)
target_link_libraries(fleet-rando INTERFACE nlohmann_json::nlohmann_json)
if(MSVC)
    target_compile_options(fleet-rando INTERFACE /bigobj /utf-8)
endif()

enable_testing()
add_executable(combo_shared_test combo/tests/combo_shared_test.cpp)
target_link_libraries(combo_shared_test PRIVATE fleet-rando)
add_test(NAME combo_shared_test COMMAND combo_shared_test WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

if(FLEET_BUILD_HOST)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    set(LUS_BUILD_SHARED ON)
    set(GBI_UCODE F3DEX_GBI_2)
    set(GBI_FLOAT_MTX ON)
    set(GBI_S32_VTX ON)
    set(INCLUDE_MPQ_SUPPORT ON)
    set(GFX_DEBUG_DISASSEMBLER ON)
    add_compile_definitions(CONTROLLERBUTTONS_T=uint32_t)
    include(cmake/lus-cvars.cmake)
    add_subdirectory(libultraship ${CMAKE_BINARY_DIR}/libultraship SYSTEM)
    find_package(libzip CONFIG REQUIRED)
    target_compile_definitions(libultraship PUBLIC INCLUDE_MPQ_SUPPORT)

    # macOS only declares the ucontext routines the game fibers switch with on request.
    if(APPLE)
        set_source_files_properties(src/host/GameFiber.cpp PROPERTIES COMPILE_DEFINITIONS "_XOPEN_SOURCE=700;_DARWIN_C_SOURCE")
    endif()

    # Every Fleet program loads the games next to its own libultraship.
    function(fleet_program target)
        add_executable(${target} src/GameModule.cpp src/host/GameInstall.cpp src/host/AssetSetup.cpp src/host/FileDialogs.cpp
                               ${ARGN})
        target_include_directories(${target} PRIVATE src)
        target_link_libraries(${target} PRIVATE libultraship)
        target_compile_definitions(${target} PRIVATE NOMINMAX)
        target_link_libraries(${target} PRIVATE monocypher libzip::zip)
        if(WIN32)
            target_link_libraries(${target} PRIVATE winhttp ole32 shell32 ws2_32 secur32)
        else()
            target_link_libraries(${target} PRIVATE ${CMAKE_DL_LIBS})
        endif()
        if(MSVC)
            set_target_properties(${target} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
        endif()
        if(APPLE)
            set_target_properties(${target} PROPERTIES BUILD_RPATH "@executable_path")
        elseif(UNIX)
            set_target_properties(${target} PROPERTIES BUILD_RPATH "$ORIGIN")
        endif()
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:libultraship> $<TARGET_FILE_DIR:${target}>)
    endfunction()

    set(FLEET_ARCHIPELAGO_SOURCES src/archipelago/ApSocket.cpp src/archipelago/ApClient.cpp
        src/archipelago/ApSession.cpp src/archipelago/ApPanel.cpp src/archipelago/ApWorld.cpp
        src/archipelago/ApInstall.cpp src/archipelago/ApExportActions.cpp)
    fleet_program(fleet src/host/main.cpp src/host/GameFiber.cpp src/host/ComboSession.cpp
                        src/host/FleetSaveMenu.cpp src/host/SeedJob.cpp src/host/PoolWorker.cpp src/host/FleetGui.cpp
                        src/seedgen/main.cpp combo/gui/ComboWidgetStyle.cpp ${FLEET_ARCHIPELAGO_SOURCES})
    target_compile_definitions(fleet PRIVATE FLEET_INTERNAL_WORKER)
    if(WIN32)
        enable_language(RC)
        target_sources(fleet PRIVATE src/windows/Fleet.rc src/windows/Fleet.manifest)
        target_include_directories(fleet PRIVATE src/windows)
    endif()
    target_link_libraries(fleet PRIVATE fleet-rando)
    if(MSVC)
        target_link_options(fleet PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
    endif()
    fleet_program(fleet-test src/host/main.cpp src/host/GameFiber.cpp src/host/ComboSession.cpp
                        src/host/FleetSaveMenu.cpp src/host/SeedJob.cpp src/host/PoolWorker.cpp src/host/FleetGui.cpp
                        src/seedgen/main.cpp combo/gui/ComboWidgetStyle.cpp ${FLEET_ARCHIPELAGO_SOURCES})
    target_link_libraries(fleet-test PRIVATE fleet-rando)
    target_compile_definitions(fleet-test PRIVATE FLEET_INTERNAL_WORKER FLEET_TEST_DRIVER)
    if(WIN32)
        target_sources(fleet-test PRIVATE src/windows/Fleet.rc src/windows/Fleet.manifest)
        target_include_directories(fleet-test PRIVATE src/windows)
    endif()
    add_executable(fleet_files_test combo/tests/fleet_files_test.cpp src/host/FleetSaveMenu.cpp src/host/SeedJob.cpp
        src/host/PoolWorker.cpp combo/gui/ComboWidgetStyle.cpp src/host/FileDialogs.cpp src/archipelago/ApWorld.cpp
        src/archipelago/ApInstall.cpp src/archipelago/ApExportActions.cpp)
    target_include_directories(fleet_files_test PRIVATE src/host src)
    target_link_libraries(fleet_files_test PRIVATE fleet-rando libultraship libzip::zip)
    if(WIN32)
        target_link_libraries(fleet_files_test PRIVATE ole32 shell32)
    endif()
    target_compile_definitions(fleet_files_test PRIVATE NOMINMAX)
    add_test(NAME fleet_files_test COMMAND fleet_files_test)
    add_executable(fleet_settings_test combo/tests/fleet_settings_test.cpp)
    target_include_directories(fleet_settings_test PRIVATE src/host)
    target_link_libraries(fleet_settings_test PRIVATE fleet-rando libultraship)
    add_test(NAME fleet_settings_test COMMAND fleet_settings_test)
    add_executable(fleet_install_test combo/tests/fleet_install_test.cpp src/host/FileDialogs.cpp)
    target_link_libraries(fleet_install_test PRIVATE fleet-rando libultraship monocypher libzip::zip)
    target_compile_definitions(fleet_install_test PRIVATE NOMINMAX)
    if(WIN32)
        target_link_libraries(fleet_install_test PRIVATE winhttp ole32 shell32)
    endif()
    add_test(NAME fleet_install_test COMMAND fleet_install_test)
    add_executable(fleet_archipelago_install_test combo/tests/fleet_archipelago_install_test.cpp
        src/archipelago/ApInstall.cpp src/archipelago/ApWorld.cpp src/host/FileDialogs.cpp)
    target_include_directories(fleet_archipelago_install_test PRIVATE src)
    target_link_libraries(fleet_archipelago_install_test PRIVATE fleet-rando libultraship libzip::zip)
    target_compile_definitions(fleet_archipelago_install_test PRIVATE NOMINMAX)
    if(WIN32)
        target_link_libraries(fleet_archipelago_install_test PRIVATE ole32 shell32)
    endif()
    add_test(NAME fleet_archipelago_install_test COMMAND fleet_archipelago_install_test)
endif()

add_executable(fleet_session_test src/host/ComboSession.cpp combo/tests/fleet_session_test.cpp)
target_include_directories(fleet_session_test PRIVATE src/host)
target_link_libraries(fleet_session_test PRIVATE fleet-rando)
target_compile_definitions(fleet_session_test PRIVATE NOMINMAX)
add_test(NAME fleet_session_test COMMAND fleet_session_test WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

add_executable(fleet_seed_job_test src/host/SeedJob.cpp src/host/PoolWorker.cpp combo/tests/fleet_seed_job_test.cpp)
target_include_directories(fleet_seed_job_test PRIVATE src/host)
target_link_libraries(fleet_seed_job_test PRIVATE nlohmann_json::nlohmann_json)
target_compile_definitions(fleet_seed_job_test PRIVATE NOMINMAX)
add_test(NAME fleet_seed_job_test COMMAND fleet_seed_job_test)
