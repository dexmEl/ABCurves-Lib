#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <abcurves.h>
#include <stdio.h>

int main(void) {
    HMODULE dll = LoadLibraryW(L"abcurves.dll");
    abc_model_handle model = 0; abc_planner_handle planner = 0; abc_segment_handle controller = 0;
    abc_renderer_handle renderer = 0; abc_report profile[256] = {{0}}, report;
    abc_planner_options options; abc_segment_options segment_options;
    abc_target_observation target = {0}; abc_segment_metadata metadata = {0};
    abc_sample samples[32]; uint64_t written = 0; int result = 1;
    if (!dll) { fputs("Cannot load abcurves.dll\n", stderr); return 1; }
#define LOAD(name) FARPROC p_##name = GetProcAddress(dll, "abc_" #name)
    LOAD(model_load_embedded); LOAD(model_release); LOAD(planner_options_init); LOAD(planner_create);
    LOAD(planner_destroy); LOAD(segment_options_init); LOAD(segment_create); LOAD(segment_destroy);
    LOAD(segment_submit_observation); LOAD(segment_next);
    LOAD(renderer_create_embedded); LOAD(renderer_destroy); LOAD(renderer_set_profile); LOAD(renderer_reset); LOAD(renderer_step);
#undef LOAD
    if (!p_model_load_embedded || !p_model_release || !p_planner_options_init || !p_planner_create ||
        !p_planner_destroy || !p_segment_options_init || !p_segment_create || !p_segment_destroy ||
        !p_segment_submit_observation || !p_segment_next || !p_renderer_create_embedded || !p_renderer_destroy ||
        !p_renderer_set_profile || !p_renderer_reset || !p_renderer_step) {
        fputs("Required ABCurves DLL export is missing\n", stderr); goto done;
    }
#define CALL(name, type, ...) ((type)p_##name)(__VA_ARGS__)
    if (CALL(model_load_embedded, abc_status (ABC_CALL *)(uint32_t, abc_model_handle*), 16, &model)) goto done;
    if (CALL(planner_options_init, abc_status (ABC_CALL *)(abc_planner_options*, uint32_t), &options, sizeof(options))) goto done;
    options.seed = 7;
    if (CALL(planner_create, abc_status (ABC_CALL *)(abc_model_handle, const abc_planner_options*, abc_planner_handle*), model, &options, &planner)) goto done;
    if (CALL(segment_options_init, abc_status (ABC_CALL *)(abc_segment_options*, uint32_t), &segment_options, sizeof(segment_options))) goto done;
    if (CALL(segment_create, abc_status (ABC_CALL *)(abc_planner_handle, const abc_segment_options*, abc_segment_handle*), planner, &segment_options, &controller)) goto done;
    target.struct_size = sizeof(target); target.kind = ABC_TARGET_RELATIVE;
    target.value.x = 120.0; target.value.y = -7.0;
    if (CALL(segment_submit_observation, abc_status (ABC_CALL *)(abc_segment_handle, const abc_target_observation*), controller, &target)) goto done;
    metadata.struct_size = sizeof(metadata);
    if (CALL(segment_next, abc_status (ABC_CALL *)(abc_segment_handle, abc_sample*, uint64_t, uint64_t*, abc_segment_metadata*), controller, samples, 32, &written, &metadata)) goto done;
    for (uint64_t i = 0; i < written; ++i) printf("%lld %.9g %.9g\n", (long long)samples[i].timestamp_us, samples[i].displacement.x, samples[i].displacement.y);
    /* Optional Renderer: the application supplies its calibrated profile and intents. */
    if (CALL(renderer_create_embedded, abc_status (ABC_CALL *)(abc_renderer_handle*), &renderer)) goto done;
    if (CALL(renderer_set_profile, abc_status (ABC_CALL *)(abc_renderer_handle, const abc_report*, uint64_t), renderer, profile, 256)) goto done;
    if (CALL(renderer_reset, abc_status (ABC_CALL *)(abc_renderer_handle, uint64_t), renderer, 7)) goto done;
    if (CALL(renderer_step, abc_status (ABC_CALL *)(abc_renderer_handle, abc_vec2, abc_report*), renderer, samples[0].displacement, &report)) goto done;
    result = 0;
done:
    if (result) fputs("ABCurves operation failed\n", stderr);
    if (renderer && p_renderer_destroy) CALL(renderer_destroy, abc_status (ABC_CALL *)(abc_renderer_handle), renderer);
    if (controller && p_segment_destroy) CALL(segment_destroy, abc_status (ABC_CALL *)(abc_segment_handle), controller);
    if (planner && p_planner_destroy) CALL(planner_destroy, abc_status (ABC_CALL *)(abc_planner_handle), planner);
    if (model && p_model_release) CALL(model_release, abc_status (ABC_CALL *)(abc_model_handle), model);
#undef CALL
    FreeLibrary(dll); return result;
}
