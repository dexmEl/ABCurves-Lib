# C++

## DLL-only quick start

On Windows x64, include `API/c/include` and `API/cpp/include` and put
`abcurves.dll` beside your executable. The DLL requires the matching Microsoft
Visual C++ runtime. This C++20 wrapper dynamically loads the DLL and needs no
ABCurves import or static library:

```cpp
#include <abcurves/dll.hpp>
#include <iostream>

int main() {
    auto dll = std::make_shared<abcurves_dll::Library>();
    auto model = abcurves_dll::Model::embedded(dll, 16);
    abcurves_dll::Planner planner(model);
    abcurves_dll::SegmentController controller(planner);
    controller.relative({120.0, -7.0}, 0, 0);
    for (const auto& sample : controller.next_segment().samples)
        std::cout << sample.timestamp_us << " " << sample.displacement.x << " " << sample.displacement.y << "\n";
}
```

See the [complete example](../../API/cpp/examples/dll.cpp) for error handling.
For repeated generation, `next_segment_into` writes into a caller-owned sample buffer. The wrapper also provides initialization history, diagnostics and an embedded Renderer; the example shows these operations. Advanced callers can resolve additional C ABI functions through `Library::resolve`.
For direct stepping and independent observations, see the [direct example](../../API/cpp/examples/dll_direct.cpp). Advance the planner through an observation's receipt time before submitting it. `advance_into` and `next_segment_into` use caller-owned buffers without wrapper allocations; `advance` and `next_segment` allocate their returned vectors. ABI failures throw `abcurves_dll::Error` with a `status()` code. `OutputError::written()` reports the valid prefix on direct advance and Renderer batch failures.

The wrapper keeps the DLL and planner alive while their dependent objects exist. Handles returned by `native_handle()` are borrowed: do not destroy them or use them after their wrapper expires. Serialize access to a planner and its attached interfaces; independent sessions can run on separate threads.
The direct native interface below remains available for source builds.

## Requirements and installation

Use C++20, CMake >=3.20, and a matching native library. Windows x64/MSVC is qualified. [Build and install from source](building.md), then link the installed CMake target:

```cmake
find_package(abcurves 1.5 CONFIG REQUIRED)
add_executable(app main.cpp)
target_compile_features(app PRIVATE cxx_std_20)
target_link_libraries(app PRIVATE abcurves::native)
```

Configure with `-DCMAKE_PREFIX_PATH=<install-prefix>`. Native C++ links the engine directly; it does not need the C ABI DLL. The installed target supplies the original Renderer-core dependency. Match the compiler/runtime configuration.

## Load, submit and generate

Save as `main.cpp`. Pass a model directory on the command line:

```cpp
#include <abcurves/native.hpp>
#include <array>
#include <iostream>

int main(int argc, char** argv) try {
    if (argc != 2) return 2;
    auto model = abc::Model::load(argv[1]);
    abc::ContinuousPlanner planner(model);
    abc::SegmentController controller(planner);
    controller.submit_observation({{120.0, -7.0}, 0, 0,
                                   abc::ObservationKind::relative});
    std::array<abc::Sample, 32> samples;
    abc::SegmentMetadata metadata;
    const auto count = controller.next_segment(samples, metadata);
    for (std::size_t i = 0; i < count; ++i)
        std::cout << samples[i].timestamp_us << " "
                  << samples[i].displacement.x << " "
                  << samples[i].displacement.y << "\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << "\n";
    return 1;
}
```

Use `lib/models/16ms` from the source tree, or `models/16ms` in a default binary SDK. Each sample is one millisecond of angular displacement. The 32-element buffer covers all current variants; only `count` entries are valid.

Run the [existing example](../../API/cpp/examples/segments.cpp) after a source build:

```powershell
build/api/Release/abc_segment_cpp_example.exe lib/models/16ms
```

The example timestamps are simulated. Your application supplies calibrated target errors, real session timestamps and physical output scheduling. Continue with [integration](../integration.md), [calibration](../guides/calibration.md), or optional [buffer/reset/lifetime details](../advanced/state-and-memory.md).
