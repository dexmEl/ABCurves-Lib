#include "abcurves.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(int argc,char** argv){
 abc_model_handle m=0;abc_planner_handle p=0;abc_observation_handle input=0;
 abc_planner_options o;abc_observation_options io;abc_target_observation ob={0};abc_vec2 result;
 abc_observation_diagnostics d={0};abc_planner_state state={0};abc_sample samples[32];uint64_t written;
 CHECK(argc==2);CHECK(abc_check_abi(1,0)==0);CHECK(abc_check_abi(1,1)==0);
 CHECK(abc_model_load(argv[1],&m)==0);CHECK(abc_planner_options_init(&o,sizeof o)==0);
 o.initial_position=(abc_vec2){100,50};CHECK(abc_planner_create(m,&o,&p)==0);CHECK(abc_model_release(m)==0);
 CHECK(abc_observation_options_init(&io,sizeof io)==0);CHECK(io.max_age_us==250000);
 CHECK(abc_observation_create(p,&io,&input)==0);
 ob.struct_size=sizeof ob;ob.kind=ABC_TARGET_RELATIVE;ob.value=(abc_vec2){12,-7};
 CHECK(abc_observation_submit(input,&ob,&result)==0);CHECK(result.x==112 && result.y==43);
 CHECK(abc_observation_submit(input,&ob,&result)==ABC_DUPLICATE_OBSERVATION);
 CHECK(abc_planner_advance(p,32000,samples,32,&written)==0 && written==32);
 ob.capture_time_us=1000;ob.receipt_time_us=32000;ob.value.x=1;
 CHECK(abc_observation_submit(input,&ob,&result)==0);
 state.struct_size=sizeof state;CHECK(abc_planner_get_state(p,&state)==0);CHECK(result.x==state.position.x+1);
 ob.capture_time_us=0;ob.receipt_time_us=33000;CHECK(abc_observation_submit(input,&ob,&result)==ABC_TIMESTAMP_RANGE);
 ob.capture_time_us=0;ob.receipt_time_us=32000;ob.value.x=2;CHECK(abc_observation_submit(input,&ob,&result)==ABC_OBSERVATION_ORDER);
 ob.capture_time_us=32000;ob.receipt_time_us=32000;ob.availability=ABC_TARGET_INVALIDATED;
 CHECK(abc_observation_submit(input,&ob,&result)==0);
 d.struct_size=sizeof d;CHECK(abc_observation_get_diagnostics(input,&d)==0);CHECK(d.accepted==3 && d.rejected==3 && d.availability==ABC_TARGET_INVALIDATED);
 CHECK(abc_planner_reset(p,7)==0);CHECK(abc_observation_get_diagnostics(input,&d)==0 && d.accepted==0);
 {abc_vec2 raw[164],prepared[160],anchor;int i;for(i=0;i<164;i++)raw[i]=(abc_vec2){1,2};
 CHECK(abc_prepare_history(raw,164,(abc_vec2){100,50},prepared,160,&anchor)==0);
 CHECK(anchor.x==98 && anchor.y==46);CHECK(fabs(prepared[159].x-1)<1e-15);
 o.initial_position=anchor;o.history=prepared;o.history_count=160;CHECK(abc_planner_reinitialize(p,&o)==0);
 o.history_count=164;CHECK(abc_planner_reinitialize(p,&o)==ABC_INVALID_ARGUMENT);}
 CHECK(abc_planner_destroy(p)==0);CHECK(abc_observation_get_diagnostics(input,&d)==ABC_INVALID_HANDLE);
 CHECK(abc_observation_destroy(input)==0);CHECK(abc_observation_destroy(input)==ABC_INVALID_HANDLE);
 puts("C99 observation contract passed");return 0;
}
