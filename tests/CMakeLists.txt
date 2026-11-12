# A test program that loads the built plug-in and drives it like a host. The tests don't link any of the plug-in's
# code: they only use one of its headers, for the parameter IDs. The analysis is compared with Pure Data's original
# fiddle~, which a host can't see the results of, by the Rust side's own tests in rust/tests/fiddle.rs, which build
# reference/ReferenceFiddle.c themselves.

add_executable(PartialResynthTests
    TestMain.cpp
    Tests.h
    Harness.h
    Harness.cpp
    CompatibilityTests.cpp
    ResynthTests.cpp
    SynthTests.cpp
)

target_include_directories(PartialResynthTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(PartialResynthTests PRIVATE hadronyche_testhost)
add_dependencies(PartialResynthTests PartialResynth)

# It drives a real window with Windows' mouse messages.
if(WIN32)
    target_sources(PartialResynthTests PRIVATE PanelTest.cpp)
endif()

set(PARTIALRESYNTH_TESTS
    class-ids parameter-ids parameter-ranges saved-format buses latency-report
    resynth-partials resynth-loudest resynth-continuous resynth-follows resynth-silence resynth-threshold resynth-count latency-alignment
    osc-waves osc-tuning osc-levels osc-aliasing
    filter-modes filter-resonance key-follow env-to-cutoff
    glide spread dry-wet volume block-sizes determinism extreme-settings parameter-state parameter-text
    panel panel-opens
)

hadronyche_add_host_tests(PartialResynthTests PartialResynth ${PARTIALRESYNTH_TESTS})

hadronyche_add_lv2_tests(PartialResynth)
