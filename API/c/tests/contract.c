#include "abcurves.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "line %d: %s (%s)\n", __LINE__, #x, abc_last_error());   \
      return 1;                                                                \
    }                                                                          \
  } while (0)
int main(int argc, char **argv) {
  abc_version_info version = {0};
  abc_model_handle m = 0;
  abc_planner_handle p = 0, q = 0;
  abc_planner_options o;
  abc_sample a[256], b[256];
  uint64_t n, i;
  abc_planner_state state = {0};
  CHECK(offsetof(abc_sample, position) == 8);
  CHECK(offsetof(abc_planner_state, position) == 80);
  if (argc != 2)
    return 2;
  version.struct_size = sizeof(version);
  CHECK(abc_get_version(&version) == 0);
  CHECK(version.sample_size == sizeof(abc_sample));
  CHECK(abc_check_abi(1, 0) == 0);
  CHECK(abc_check_abi(2, 0) == ABC_UNSUPPORTED_VERSION);
  CHECK(abc_model_load(NULL, &m) == ABC_NULL_POINTER);
  CHECK(m == 0);
  CHECK(abc_planner_get_state(0, &state) == ABC_INVALID_HANDLE);
  CHECK(abc_model_load(argv[1], &m) == 0);
  CHECK(abc_planner_options_init(&o, sizeof(o)) == 0);
  o.flags = 2;
  CHECK(abc_planner_create(m, &o, &p) == ABC_INVALID_ARGUMENT && p == 0);
  o.flags = 0;
  o.seed = 11;
  CHECK(abc_planner_create(m, &o, &p) == 0);
  CHECK(abc_planner_create(m, &o, &q) == 0);
  CHECK(abc_model_release(m) == 0);
  CHECK(abc_model_release(m) == ABC_INVALID_HANDLE);
  CHECK(abc_planner_destroy(q + 1) == ABC_INVALID_HANDLE);
  CHECK(abc_planner_update_target(p, (abc_vec2){320, 120}, 0) == 0);
  CHECK(abc_planner_update_target(q, (abc_vec2){320, 120}, 0) == 0);
  CHECK(abc_planner_required_capacity(p, INT64_MAX, &n) == 0);
  CHECK(n == (uint64_t)INT64_MAX / 1000);
  CHECK(abc_planner_advance(p, INT64_MAX, a, 256, &n) ==
            ABC_INSUFFICIENT_CAPACITY &&
        n == 0);
  CHECK(abc_planner_advance(p, 1000, a, UINT64_MAX, &n) ==
            ABC_INVALID_ARGUMENT &&
        n == 0);
  CHECK(abc_planner_advance(p, 1000, NULL, 1, &n) == ABC_NULL_POINTER &&
        n == 0);
  CHECK(abc_planner_advance(p, 1000, a, 1, NULL) == ABC_NULL_POINTER);
  CHECK(abc_planner_generate_sample(p, 2000, a) == ABC_INVALID_ARGUMENT);
  CHECK(abc_planner_update_target(p, (abc_vec2){NAN, 0}, 0) ==
        ABC_INVALID_ARGUMENT);
  n = 99;
  CHECK(abc_planner_advance(p, 256000, a, 255, &n) ==
            ABC_INSUFFICIENT_CAPACITY &&
        n == 0);
  CHECK(abc_planner_advance(p, 500, NULL, 0, &n) == 0 && n == 0);
  CHECK(abc_planner_advance(p, 400, NULL, 0, &n) == ABC_TIMESTAMP_RANGE);
  CHECK(abc_planner_advance(p, 256000, a, 256, &n) == 0 && n == 256);
  for (i = 0; i < 256; i++)
    CHECK(abc_planner_generate_sample(q, (int64_t)(i + 1) * 1000, &b[i]) == 0);
  CHECK(memcmp(a, b, sizeof(a)) == 0);
  state.struct_size = sizeof(state);
  CHECK(abc_planner_get_state(p, &state) == 0 &&
        state.numerical_time_us == 256000);
  CHECK(abc_planner_reset(p, 11) == 0);
  CHECK(abc_planner_update_target(p, (abc_vec2){320, 120}, 0) == 0);
  CHECK(abc_planner_advance(p, 256000, b, 256, &n) == 0);
  CHECK(memcmp(a, b, sizeof(a)) == 0);
  CHECK(abc_planner_clear_target(p, 256000) == 0);
  CHECK(abc_planner_set_position(p, (abc_vec2){1, 2}) == 0);
  CHECK(abc_planner_destroy(p) == 0);
  CHECK(abc_planner_destroy(q) == 0);
  CHECK(abc_planner_reset(p, 1) == ABC_INVALID_HANDLE);
  printf("C99 lifecycle, stale handles, ABI layouts, fractional clocks, reset "
         "and batch parity: "
         "pass\n");
  return 0;
}
