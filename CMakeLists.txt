cmake_minimum_required(VERSION 3.24)

project(Nenia
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(NENIA_BUILD_FLUIDSYNTH_PROTOTYPE
    "Build offline FluidSynth synthesis support" OFF)
option(NENIA_BUILD_MIDI_PLAYBACK_BACKEND
    "Build the realtime MIDI backend and enable application integration" OFF)

# Keep the private development checkout testable by default, while allowing
# public source archives that omit tests/ to configure as production builds.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    set(_nenia_build_testing_default ON)
else()
    set(_nenia_build_testing_default OFF)
endif()
option(BUILD_TESTING "Build Nenia regression tests" ${_nenia_build_testing_default})
option(NENIA_BUILD_DIAGNOSTIC_TOOLS
    "Build private command-line audio and MIDI diagnostic tools" ${BUILD_TESTING})

include(CTest)

if(BUILD_TESTING AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    message(FATAL_ERROR
        "BUILD_TESTING=ON was requested, but tests/CMakeLists.txt is absent. "
        "The public source archive excludes private development tests; "
        "configure it with -DBUILD_TESTING=OFF.")
endif()

if(NENIA_BUILD_MIDI_PLAYBACK_BACKEND AND NOT NENIA_BUILD_FLUIDSYNTH_PROTOTYPE)
    message(FATAL_ERROR
        "NENIA_BUILD_MIDI_PLAYBACK_BACKEND requires NENIA_BUILD_FLUIDSYNTH_PROTOTYPE=ON "
        "(the realtime backend reuses the offline FluidSynth implementation).")
endif()

find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui Widgets Multimedia)
if(BUILD_TESTING)
    find_package(Qt6 6.12 REQUIRED COMPONENTS Test)
endif()
if(UNIX AND NOT APPLE)
    find_package(Qt6 6.12 QUIET COMPONENTS DBus)
endif()
find_package(TagLib 2.0 CONFIG REQUIRED)

if(NENIA_BUILD_FLUIDSYNTH_PROTOTYPE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FLUIDSYNTH REQUIRED IMPORTED_TARGET "fluidsynth=2.3.5")
    if(NOT FLUIDSYNTH_VERSION STREQUAL "2.3.5")
        message(FATAL_ERROR
            "Offline FluidSynth synthesis requires version 2.3.5 exactly; found ${FLUIDSYNTH_VERSION}")
    endif()
    message(STATUS "Offline FluidSynth synthesis: version ${FLUIDSYNTH_VERSION}")
endif()

if(NENIA_BUILD_MIDI_PLAYBACK_BACKEND)
    message(STATUS "Realtime MIDI playback backend: enabled")
endif()

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

qt_standard_project_setup()

function(nenia_enable_warnings target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    elseif(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    endif()
endfunction()

add_subdirectory(src)
add_subdirectory(app)

if(NENIA_BUILD_DIAGNOSTIC_TOOLS)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/CMakeLists.txt")
        message(FATAL_ERROR
            "NENIA_BUILD_DIAGNOSTIC_TOOLS=ON was requested, but private diagnostic sources are absent.")
    endif()
    add_subdirectory(tools)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

set(NENIA_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/Nenia")

install(TARGETS nenia_lib
    EXPORT NeniaTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(DIRECTORY include/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(EXPORT NeniaTargets
    FILE NeniaTargets.cmake
    NAMESPACE Nenia::
    DESTINATION "${NENIA_CMAKE_INSTALL_DIR}"
)

configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/NeniaConfig.cmake.in"
    "${PROJECT_BINARY_DIR}/NeniaConfig.cmake"
    INSTALL_DESTINATION "${NENIA_CMAKE_INSTALL_DIR}"
)

write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/NeniaConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${PROJECT_BINARY_DIR}/NeniaConfig.cmake"
    "${PROJECT_BINARY_DIR}/NeniaConfigVersion.cmake"
    DESTINATION "${NENIA_CMAKE_INSTALL_DIR}"
)

set(NENIA_LICENSE_INSTALL_DIR "${CMAKE_INSTALL_DATADIR}/licenses/Nenia")
install(FILES
    "${PROJECT_SOURCE_DIR}/LICENSE"
    "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
    DESTINATION "${NENIA_LICENSE_INSTALL_DIR}"
)
install(FILES
    "${PROJECT_SOURCE_DIR}/assets/fonts/LICENSE-Source-Code-Pro-OFL.txt"
    "${PROJECT_SOURCE_DIR}/assets/fonts/LICENSE-Fusion-Pixel-OFL.txt"
    DESTINATION "${NENIA_LICENSE_INSTALL_DIR}/fonts"
)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/assets/fonts/LICENSES/"
    DESTINATION "${NENIA_LICENSE_INSTALL_DIR}/fonts/LICENSES"
)
