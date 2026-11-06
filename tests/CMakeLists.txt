# A test program that loads the built plug-in and drives it like a host. The host-style tests don't link any
# of the plug-in's code: they only use one of its headers, for the parameter IDs.
#
# The exception is the analysis, which is compiled in here to compare it, frame by frame, with Pure Data's
# original fiddle~, which a host can't see the results of.

add_library(ReferenceFiddle STATIC reference/ReferenceFiddle.c)
target_include_directories(ReferenceFiddle PUBLIC reference)
if(MSVC)
    # The stub of Pd's API has more in it than fiddle~ uses, and the original wasn't written for /W3.
    target_compile_options(ReferenceFiddle PRIVATE /w)
endif()

add_executable(PartialResynthTests
    TestMain.cpp
    Tests.h
    Harness.h
    Harness.cpp
    CompatibilityTests.cpp
    FiddleTests.cpp
    ResynthTests.cpp
    SynthTests.cpp
    PanelTest.cpp
    "${CMAKE_SOURCE_DIR}/src/dsp/FiddleAnalyzer.cpp"
    "${VST3_SDK_DIR}/pluginterfaces/base/funknown.cpp"
    "${VST3_SDK_DIR}/pluginterfaces/base/coreiids.cpp"
    "${VST3_SDK_DIR}/pluginterfaces/base/ustring.cpp"
    "${VST3_SDK_DIR}/public.sdk/source/common/memorystream.cpp"
)

target_include_directories(PartialResynthTests PRIVATE "${VST3_SDK_DIR}" "${CMAKE_SOURCE_DIR}/src")
target_compile_definitions(PartialResynthTests PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(PartialResynthTests PRIVATE hadronyche_params ReferenceFiddle user32 gdi32)
add_dependencies(PartialResynthTests PartialResynth)

# One CTest test per test in the program, so that a single one can be run with ctest -R.
set(PARTIALRESYNTH_TESTS
    class-ids parameter-ids parameter-ranges saved-format buses latency-report
    fiddle-match fiddle-accuracy fiddle-floor fiddle-power fiddle-sample-rates
    tracker-loudest tracker-continuity tracker-fade tracker-new-waits tracker-keeps-place
    resynth-partials resynth-loudest resynth-continuous resynth-follows resynth-silence resynth-threshold latency-alignment
    osc-waves osc-tuning osc-levels osc-aliasing
    filter-modes filter-resonance key-follow env-to-cutoff
    glide spread dry-wet volume block-sizes determinism extreme-settings parameter-state parameter-text
    panel
)

foreach(test_name ${PARTIALRESYNTH_TESTS})
    add_test(NAME ${test_name} COMMAND PartialResynthTests "$<TARGET_FILE:PartialResynth>" ${test_name})
    set_tests_properties(${test_name} PROPERTIES TIMEOUT 300)
endforeach()
