# C

For a DLL-only Windows x64 application, use the [dynamic-loading example](../../API/c/examples/dll.c).
It loads `abcurves.dll` with `LoadLibraryW` and needs the public header, but no
ABCurves import library. The following example uses conventional linking.

## Requirements and installation

Your application can use C99. Building the engine requires C++20 and CMake >=3.20; Windows x64/MSVC is qualified. [Build/install the SDK](building.md), then:

```cmake
find_package(abcurves 1.5 CONFIG REQUIRED)
add_executable(app main.c)
set_target_properties(app PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES)
target_link_libraries(app PRIVATE abcurves::c)
```

Configure with `-DCMAKE_PREFIX_PATH=<install-prefix>`. At runtime put the SDK's `bin/abcurves.dll` beside the application or its `bin` directory on PATH. The matching Microsoft Visual C++ runtime is required. No additional Renderer DLL is needed.

## Load, submit and generate

Save as `main.c`. Initialize options, size-tag structures, check statuses, and destroy handles when finished:

```c
#include <abcurves.h>
#include <stdio.h>
#define CHECK(call) do { status = (call); if (status != ABC_OK) goto cleanup; } while (0)

int main(void) {
    abc_model_handle model = 0;
    abc_planner_handle planner = 0;
    abc_segment_handle controller = 0;
    abc_status status = ABC_OK;
    abc_planner_options options = {0};
    abc_segment_options segment_options = {0};
    abc_target_observation target = {0};
    abc_segment_metadata metadata = {0};
    abc_sample samples[32];
    uint64_t count = 0, i;
    CHECK(abc_model_load_embedded(16, &model));
    CHECK(abc_planner_options_init(&options, sizeof(options)));
    CHECK(abc_segment_options_init(&segment_options, sizeof(segment_options)));
    CHECK(abc_planner_create(model, &options, &planner));
    CHECK(abc_segment_create(planner, &segment_options, &controller));
    target.struct_size = sizeof(target);
    target.kind = ABC_TARGET_RELATIVE;
    target.availability = ABC_TARGET_AVAILABLE;
    target.value.x = 120.0; target.value.y = -7.0;
    target.capture_time_us = target.receipt_time_us = 0;
    CHECK(abc_segment_submit_observation(controller, &target));
    metadata.struct_size = sizeof(metadata);
    CHECK(abc_segment_next(controller, samples, 32, &count, &metadata));
    for (i = 0; i < count; ++i)
        printf("%lld %.9f %.9f\n", (long long)samples[i].timestamp_us,
               samples[i].displacement.x, samples[i].displacement.y);
cleanup:
    if (status != ABC_OK) fprintf(stderr, "%s\n", abc_last_error());
    if (controller) abc_segment_destroy(controller);
    if (planner) abc_planner_destroy(planner);
    if (model) abc_model_release(model);
    return status == ABC_OK ? 0 : 1;
}
```

The model is embedded in the DLL. The 32-element buffer covers current models; iterate only `count`. Samples are angular displacements, not device reports. Zero is a simulated session timestamp here. External bundles can still be loaded with `abc_model_load`.

A [ready-to-run source example](../../API/c/examples/segments.c) is built by CMake:

```powershell
build/api/Release/abc_segment_c_example.exe lib/models/16ms
```

Continue with [integration](../integration.md), [calibration](../guides/calibration.md) and [errors](../advanced/diagnostics.md). The [advanced C ABI reference](../advanced/legacy-api.md) covers optional introspection and exact generated signatures.
