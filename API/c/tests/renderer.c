#include "abcurves.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "line %d: %s: %s\n", __LINE__, #x, abc_last_error());    \
      return 1;                                                                \
    }                                                                          \
  } while (0)
int main(int argc, char **argv) {
  FILE *f;
  unsigned long long n, seed;
  double scale;
  abc_renderer_handle r = 0;
  abc_report profile[256], *batch, *single;
  abc_vec2 *intents;
  uint64_t written;
  size_t i;
  if (argc != 3)
    return 2;
  f = fopen(argv[2], "r");
  CHECK(f);
  CHECK(fscanf(f, "%llu %lf %llu", &n, &scale, &seed) == 3);
  for (i = 0; i < 256; i++) {
    int x, y;
    CHECK(fscanf(f, "%d %d", &x, &y) == 2);
    profile[i].dx = (int16_t)x;
    profile[i].dy = (int16_t)y;
  }
  intents = (abc_vec2 *)calloc((size_t)n, sizeof(*intents));
  batch = (abc_report *)calloc((size_t)n, sizeof(*batch));
  single = (abc_report *)calloc((size_t)n, sizeof(*single));
  CHECK(intents && batch && single);
  for (i = 0; i < (size_t)n; i++) {
    CHECK(fscanf(f, "%lf %lf", &intents[i].x, &intents[i].y) == 2);
    intents[i].x /= scale;
    intents[i].y /= scale;
  }
  fclose(f);
  CHECK(abc_renderer_create(argv[1], &r) == 0);
  CHECK(abc_renderer_reset(r, seed) == ABC_INVALID_STATE);
  CHECK(abc_renderer_set_profile(r, profile, 255) == ABC_INVALID_ARGUMENT);
  CHECK(abc_renderer_set_profile(r, profile, 256) == 0);
  CHECK(abc_renderer_reset(r, seed) == 0);
  CHECK(abc_renderer_step_batch(r, intents, n, batch, n - 1, &written) ==
            ABC_INSUFFICIENT_CAPACITY &&
        written == 0);
  CHECK(abc_renderer_step_batch(r, intents, n, batch, n, &written) == 0 &&
        written == n);
  CHECK(abc_renderer_reset(r, seed) == 0);
  for (i = 0; i < (size_t)n; i++)
    CHECK(abc_renderer_step(r, intents[i], &single[i]) == 0);
  CHECK(memcmp(single, batch, (size_t)n * sizeof(*single)) == 0);
  CHECK(abc_renderer_step_batch(r, NULL, 0, NULL, 0, &written) == 0 &&
        written == 0);
  CHECK(abc_renderer_destroy(r) == 0);
  free(intents);
  free(batch);
  free(single);
  puts("C99 persistent Renderer reset/batch/single: pass");
  return 0;
}
