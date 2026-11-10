#include "abcurves.h"
#include <stdio.h>
int main(int argc, char **argv) {
  abc_model_handle m = 0;
  abc_planner_handle p = 0;
  abc_sample samples[128];
  uint64_t n = 0;
  abc_status status;
  if (argc != 2) {
    fprintf(stderr, "usage: planner <model-directory>\n");
    return 2;
  }
  if ((status = abc_model_load(argv[1], &m)) != 0)
    goto done;
  if ((status = abc_planner_create(m, NULL, &p)) != 0)
    goto done;
  abc_model_release(m);
  m = 0;
  if ((status = abc_planner_update_target(p, (abc_vec2){320, 120}, 0)) != 0)
    goto done;
  if ((status = abc_planner_advance(p, 128000, samples, 128, &n)) != 0)
    goto done;
  printf("%llu samples, final=(%.9f, %.9f)\n", (unsigned long long)n, samples[n - 1].position.x,
         samples[n - 1].position.y);
done:
  if (status)
    fprintf(stderr, "%s: %s\n", abc_status_message(status), abc_last_error());
  if (p)
    abc_planner_destroy(p);
  if (m)
    abc_model_release(m);
  return status ? 1 : 0;
}
