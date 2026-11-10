cmake_minimum_required(VERSION 3.20)
project(abcurves_native VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")

option(ABC_BUILD_TESTS "Build native unit and smoke tests" ON)
option(ABC_BUILD_BENCHMARKS "Build reproducible benchmarks and replay tools" ON)
option(ABC_BUILD_EXAMPLES "Build native examples" ON)
option(ABC_ENABLE_AVX2 "Runtime-dispatched AVX2 kernels with portable fallback" ON)
option(ABC_PROFILE "Instrument component timings (not for official performance)" OFF)
set(ABC_RENDERER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Native/runtime/c" CACHE PATH "Existing C99 Renderer source")

if(EXISTS "${ABC_RENDERER_SOURCE_DIR}/CMakeLists.txt")
    add_subdirectory("${ABC_RENDERER_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/renderer")
else()
    message(FATAL_ERROR "The existing C99 Renderer was not found at ${ABC_RENDERER_SOURCE_DIR}")
endif()

add_library(abcurves_native STATIC
    src/core/planner.cpp
    src/core/observation.cpp
    src/core/segments.cpp
    src/core/physical.cpp
    src/core/neural.cpp
    src/core/matvec.cpp
    src/models/loader.cpp
    src/renderer/renderer_adapter.cpp
)
add_library(abcurves::native ALIAS abcurves_native)
target_include_directories(abcurves_native
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>" "$<INSTALL_INTERFACE:include>"
    PRIVATE "${ABC_RENDERER_SOURCE_DIR}/include")
target_link_libraries(abcurves_native PUBLIC abcurves_renderer_core)
target_compile_features(abcurves_native PUBLIC cxx_std_20)
target_compile_definitions(abcurves_native PUBLIC ABC_ENGINE_VERSION_MAJOR=0 ABC_ENGINE_VERSION_MINOR=2 ABC_ENGINE_VERSION_PATCH=0)
if(ABC_PROFILE)
    target_compile_definitions(abcurves_native PUBLIC ABC_PROFILE)
endif()
if(ABC_ENABLE_AVX2 AND CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|amd64|x86_64|x64")
    target_sources(abcurves_native PRIVATE src/core/matvec_avx2.cpp)
    target_compile_definitions(abcurves_native PRIVATE ABC_AVX2)
    if(MSVC)
        set_source_files_properties(src/core/matvec_avx2.cpp PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
    else()
        set_source_files_properties(src/core/matvec_avx2.cpp PROPERTIES COMPILE_OPTIONS "-mavx2;-ffp-contract=off")
    endif()
endif()
if(MSVC)
    target_compile_definitions(abcurves_native PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(abcurves_native PRIVATE /W4 /permissive- /fp:strict)
else()
    target_compile_options(abcurves_native PRIVATE -Wall -Wextra -Wpedantic -fno-fast-math -ffp-contract=off)
endif()

add_custom_target(export_models
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/models"
    COMMAND python "${CMAKE_CURRENT_SOURCE_DIR}/tools/export_models.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
    COMMENT "Exporting the external 32/16/8 ms model bundles")

if(ABC_BUILD_TESTS)
    enable_testing()
    add_executable(abcurves_native_unit_tests tests/unit/native_unit.cpp)
    target_link_libraries(abcurves_native_unit_tests PRIVATE abcurves::native)
    add_test(NAME abcurves_native_unit COMMAND abcurves_native_unit_tests)

    add_executable(abcurves_native_smoke tests/integration/planner_smoke.cpp)
    target_link_libraries(abcurves_native_smoke PRIVATE abcurves::native)
    add_test(NAME abcurves_native_smoke COMMAND abcurves_native_smoke "${CMAKE_CURRENT_SOURCE_DIR}/models/32ms")
    add_executable(abcurves_numerical_probe tests/differential/numerical_probe.cpp)
    target_include_directories(abcurves_numerical_probe PRIVATE src/core)
    target_link_libraries(abcurves_numerical_probe PRIVATE abcurves::native)
    add_executable(abcurves_planner_contract tests/integration/planner_contract.cpp)
    target_link_libraries(abcurves_planner_contract PRIVATE abcurves::native)
    add_test(NAME abcurves_planner_contract COMMAND abcurves_planner_contract "${CMAKE_CURRENT_SOURCE_DIR}/models")
    add_executable(abcurves_renderer_probe tests/differential/renderer_probe.cpp)
    target_link_libraries(abcurves_renderer_probe PRIVATE abcurves::native)
endif()

if(ABC_BUILD_EXAMPLES)
    add_executable(abcurves_planner_only examples/planner_only.cpp)
    target_link_libraries(abcurves_planner_only PRIVATE abcurves::native)
    add_executable(abcurves_planner_renderer examples/planner_renderer.cpp)
    target_link_libraries(abcurves_planner_renderer PRIVATE abcurves::native)
endif()

if(ABC_BUILD_BENCHMARKS)
add_executable(abcurves_native_benchmark benchmarks/native_benchmark.cpp)
target_link_libraries(abcurves_native_benchmark PRIVATE abcurves::native)
add_executable(abcurves_replay benchmarks/replay.cpp)
target_include_directories(abcurves_replay PRIVATE src/core)
target_compile_definitions(abcurves_replay PRIVATE ABC_EXTENDED_DIAGNOSTICS)
target_link_libraries(abcurves_replay PRIVATE abcurves::native)
if(WIN32)
    target_link_libraries(abcurves_replay PRIVATE psapi)
endif()
add_executable(abcurves_pipeline_benchmark benchmarks/pipeline_benchmark.cpp)
target_link_libraries(abcurves_pipeline_benchmark PRIVATE abcurves::native)

endif()

set_target_properties(abcurves_native PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")

if(ABC_BUILD_TESTS)
add_executable(abcurves_renderer_state_probe tests/differential/renderer_state_probe.cpp)
target_link_libraries(abcurves_renderer_state_probe PRIVATE abcurves_renderer_core)
endif()

if(ABC_BUILD_TESTS)
 add_executable(abcurves_observation_contract tests/integration/observation_contract.cpp)
 target_link_libraries(abcurves_observation_contract PRIVATE abcurves::native)
 add_test(NAME abcurves_observation_contract COMMAND abcurves_observation_contract "${CMAKE_CURRENT_SOURCE_DIR}/models")
endif()


if(ABC_BUILD_TESTS)
 add_executable(abcurves_segment_contract tests/integration/segment_contract.cpp)
 target_link_libraries(abcurves_segment_contract PRIVATE abcurves::native)
 add_test(NAME abcurves_segment_contract COMMAND abcurves_segment_contract "${CMAKE_CURRENT_SOURCE_DIR}/models" "${CMAKE_CURRENT_SOURCE_DIR}/../Native/models/renderer_global_h80.bin")
 if(ABC_ENABLE_ASAN)
  # All scenarios and 4096-segment runs remain; shorten repeated Renderer
  # stress under instrumentation. Full Release/Debug uses 1100 per scenario.
  set_tests_properties(abcurves_segment_contract PROPERTIES ENVIRONMENT "ABC_SEGMENT_TEST_ROUNDS=200")
 endif()
endif()

if(ABC_BUILD_TESTS)
add_executable(abcurves_segment_recovery tests/integration/segment_recovery.cpp)
target_link_libraries(abcurves_segment_recovery PRIVATE abcurves::native)
add_test(NAME abcurves_segment_recovery COMMAND abcurves_segment_recovery "${CMAKE_CURRENT_SOURCE_DIR}/models" "${CMAKE_CURRENT_SOURCE_DIR}/../Native/models/renderer_global_h80.bin")
if(ABC_ENABLE_ASAN)
 if(MSVC)
  target_compile_options(abcurves_segment_recovery PRIVATE /fsanitize=address /Zi)
  target_link_options(abcurves_segment_recovery PRIVATE /INCREMENTAL:NO)
 else()
  target_compile_options(abcurves_segment_recovery PRIVATE -fsanitize=address -fno-omit-frame-pointer)
  target_link_options(abcurves_segment_recovery PRIVATE -fsanitize=address)
 endif()
endif()

endif() # recovery test
