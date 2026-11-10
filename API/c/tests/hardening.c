#include "abcurves.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s (%s)\n",__LINE__,#x,abc_last_error());return 1;}}while(0)
#define OK(x) CHECK((x)==ABC_OK)
int main(int argc,char**argv) {
 abc_model_handle model;abc_planner_handle p;abc_segment_handle s,other=123;abc_observation_handle input;
 abc_planner_state st={0};abc_segment_state state={0};abc_segment_metadata m={0};abc_segment_diagnostics d={0};abc_segment_recovery_diagnostics r={0};
 abc_target_observation o={0};abc_sample out[32],ref[32];abc_vec2 resolved;uint64_t n,cap;uint32_t size;
 CHECK(argc==2);OK(abc_model_load(argv[1],&model));OK(abc_planner_create(model,0,&p));OK(abc_observation_create(p,0,&input));OK(abc_segment_create(p,0,&s));
 CHECK(abc_segment_create(p,0,&other)==4 && other==0);
 CHECK(abc_planner_set_position(p,(abc_vec2){99,22})==4);
 CHECK(abc_planner_update_target(p,(abc_vec2){320,120},0)==4);
 CHECK(abc_planner_clear_target(p,0)==4);
 CHECK(abc_planner_advance(p,1000,out,32,&n)==4 && n==0);
 o.struct_size=sizeof o;o.value=(abc_vec2){320,120};CHECK(abc_observation_submit(input,&o,&resolved)==4);
 st.struct_size=sizeof st;OK(abc_planner_get_state(p,&st));CHECK(!st.failed && st.requested_time_us==0);
 OK(abc_segment_required_capacity(s,&cap));CHECK(cap==8 || cap==16 || cap==32);
 state.struct_size=sizeof state;OK(abc_segment_state_layout(&size));CHECK(size==sizeof state);
 m.struct_size=sizeof m;OK(abc_segment_next(s,out,32,&n,&m));CHECK(n==1);
 o.capture_time_us=o.receipt_time_us=1000;o.value=(abc_vec2){1.3e308,1.3e308};OK(abc_segment_submit_observation(s,&o));
 CHECK(abc_segment_next(s,out,32,&n,&m)==13 && n==0);
 OK(abc_planner_get_state(p,&st));CHECK(st.failed && st.requested_time_us>st.numerical_time_us);
 d.struct_size=sizeof d;OK(abc_segment_get_diagnostics(s,&d));CHECK(d.segment_generation_errors==1);
 r.struct_size=sizeof r;OK(abc_segment_get_recovery_diagnostics(s,&r));CHECK(r.recoverable_observation_errors==0);
 OK(abc_segment_get_state(s,&state));CHECK(state.failure_status==13 && state.failure_category==ABC_FAILURE_NUMERICAL && state.reset_required);
 CHECK(state.last_segment_id==1 && state.last_segment_end_us==1000 && strstr(state.error_message,"nonfinite"));
 OK(abc_segment_get_metadata(s,&m));CHECK(m.segment_id==1);
 CHECK(abc_segment_next(s,out,32,&n,&m)==4);OK(abc_segment_get_state(s,&state));CHECK(state.failure_status==13);
 OK(abc_segment_reset_session(s));OK(abc_segment_get_state(s,&state));CHECK(!state.reset_required);
 OK(abc_segment_destroy(s));OK(abc_observation_destroy(input));
 /* Stored seed/prefix and explicit seed reset preserve old trajectories. */
 {
  abc_planner_options options;abc_vec2 history[160];abc_planner_handle q;int k,j;
  for(k=0;k<160;++k)history[k]=(abc_vec2){.03,-.01};
  OK(abc_planner_options_init(&options,sizeof options));options.seed=11;options.history=history;options.history_count=160;
  OK(abc_planner_reinitialize(p,&options));OK(abc_planner_create(model,&options,&q));
  for(j=0;j<3;++j){
   OK(abc_planner_update_target(p,(abc_vec2){320,120},0));OK(abc_planner_update_target(q,(abc_vec2){320,120},0));
   OK(abc_planner_advance(p,cap*1000,out,32,&n));OK(abc_planner_advance(q,cap*1000,ref,32,&n));CHECK(memcmp(out,ref,(size_t)n*sizeof out[0])==0);
   if(j==0){OK(abc_planner_reset_session(p));OK(abc_planner_reset_session(q));}
   else {OK(abc_planner_reset(p,7));OK(abc_planner_reset(q,7));}
  }
  OK(abc_planner_destroy(q));
 }
 /* Legacy partial-output semantics remain intact on a later failed decision. */
 {
  abc_sample many[128];
  OK(abc_planner_reset_session(p));OK(abc_planner_update_target(p,(abc_vec2){320,120},0));
  OK(abc_planner_update_target(p,(abc_vec2){1.3e308,1.3e308},(int64_t)cap*2000));
  CHECK(abc_planner_advance(p,128000,many,128,&n)==13);
  OK(abc_planner_get_state(p,&st));CHECK(st.failed && st.requested_time_us==128000);
  CHECK(n>0 && (int64_t)n*1000==st.numerical_time_us && st.numerical_time_us<128000);
 }
 OK(abc_planner_reset_session(p));OK(abc_segment_create(p,0,&s));OK(abc_planner_destroy(p));
 CHECK(abc_segment_get_state(s,&state)==2);OK(abc_segment_destroy(s));OK(abc_model_release(model));
 puts("C hardening passed");return 0;
}
