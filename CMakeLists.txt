cmake_minimum_required(VERSION 3.25)

# The oldest macOS it runs on. This has to come before project (), or it defaults to the build machine's own.
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "The oldest macOS supported")

# C is only used by the tests, which build Pure Data's original fiddle~ to compare the analysis against.
project(PartialResynth VERSION 1.5.0 LANGUAGES C CXX)

# Can point at another checkout, to work on the library and a plug-in together.
set(HADRONYCHE_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/hadronychecore" CACHE PATH "The HadronycheCore library")

if(NOT EXISTS "${HADRONYCHE_CORE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "HadronycheCore submodule is missing. Run: git submodule update --init --recursive")
endif()

include("${HADRONYCHE_CORE_DIR}/cmake/HadronychePlugin.cmake")

hadronyche_use_vst3_sdk()

# --- Plug-in ------------------------------------------------------------------
hadronyche_add_rust(PartialResynth)
hadronyche_add_plugin(PartialResynth AU_TYPE aufx AU_SUBTYPE PrRs SOURCES
    src/PluginConfig.h
    src/PluginEntry.cpp
    src/ParamIds.h
    src/Processor.h
    src/Processor.cpp
    src/Controller.h
    src/Controller.cpp
    src/Editor.h
    src/Editor.cpp
)

# --- LV2 ----------------------------------------------------------------------
hadronyche_add_lv2(PartialResynth)

# --- Tests --------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
