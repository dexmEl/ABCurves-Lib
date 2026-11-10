#include "abcurves.h"
#include <stdio.h>
#define TRY(x) do {if((x)!=ABC_OK){fprintf(stderr,"%s\n",abc_last_error());return 1;}}while(0)
int main(int argc,char**argv){
 abc_model_handle model;abc_planner_handle planner;abc_observation_handle input;
 abc_target_observation observation={0};abc_vec2 target;abc_sample samples[128];uint64_t written;
 if(argc!=2)return 2;
 TRY(abc_model_load(argv[1],&model));TRY(abc_planner_create(model,NULL,&planner));
 TRY(abc_model_release(model));TRY(abc_observation_create(planner,NULL,&input));
 TRY(abc_planner_advance(planner,12000,samples,128,&written));
 observation.struct_size=sizeof observation;observation.kind=ABC_TARGET_RELATIVE;
 observation.capture_time_us=0;observation.receipt_time_us=12000;
 observation.value=(abc_vec2){12,-7};
 TRY(abc_observation_submit(input,&observation,&target));
 printf("target %.3f %.3f\n",target.x,target.y);
 TRY(abc_planner_advance(planner,24000,samples,128,&written));
 TRY(abc_observation_destroy(input));TRY(abc_planner_destroy(planner));return 0;
}
