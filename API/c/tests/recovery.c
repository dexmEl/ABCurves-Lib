#include "abcurves.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d %s: %s\n",__LINE__,#x,abc_last_error());exit(1);}}while(0)
int main(int argc,char**argv){
 abc_model_handle m;abc_planner_handle p;abc_segment_handle s;abc_planner_options options;
 abc_segment_recovery_diagnostics d;abc_segment_metadata metadata;abc_target_observation o;abc_sample samples[32];uint64_t n;uint32_t size;
 CHECK(argc==2);CHECK(abc_check_abi(1,2)==0 && abc_check_abi(1,3)==0);
 CHECK(abc_segment_recovery_layout(&size)==0 && size==sizeof(d));
 CHECK(abc_model_load(argv[1],&m)==0);CHECK(abc_planner_options_init(&options,sizeof options)==0);
 options.initial_position.x=options.initial_position.y=1.3e308;CHECK(abc_planner_create(m,&options,&p)==0);CHECK(abc_segment_create(p,0,&s)==0);
 memset(&o,0,sizeof o);o.struct_size=sizeof o;o.kind=1;o.capture_time_us=o.receipt_time_us=1000;o.value.x=o.value.y=1.3e308;
 CHECK(abc_segment_submit_observation(s,&o)==0);metadata.struct_size=sizeof metadata;
 CHECK(abc_segment_next(s,samples,32,&n,&metadata)==0 && n==1);
 CHECK(abc_segment_next(s,samples,32,&n,&metadata)==0 && metadata.end_timestamp_us==2000);
 d.struct_size=sizeof d;CHECK(abc_segment_get_recovery_diagnostics(s,&d)==0);
 CHECK(d.last_error_status==1 && d.invalid_pending_observations==1 && d.nonfinite_relative_resolutions==1 && d.pending_observations==0);
 o.kind=0;o.capture_time_us=o.receipt_time_us=2000;CHECK(abc_segment_submit_observation(s,&o)==0);
 CHECK(abc_segment_next(s,samples,32,&n,&metadata)==0);
 o.capture_time_us=o.receipt_time_us=1000000;CHECK(abc_segment_submit_observation(s,&o)==0);
 CHECK(abc_segment_clear_pending_observations(s)==0);CHECK(abc_segment_clear_pending_observations(s)==0);
 CHECK(abc_segment_get_recovery_diagnostics(s,&d)==0 && d.explicit_pending_clears==2 && d.pending_observations==0);
 CHECK(abc_segment_next(s,samples,32,&n,&metadata)==0 && n==metadata.variant_ms);
 CHECK(abc_planner_destroy(p)==0);CHECK(abc_segment_clear_pending_observations(s)==ABC_INVALID_HANDLE);
 CHECK(abc_segment_get_recovery_diagnostics(s,&d)==ABC_INVALID_HANDLE);CHECK(abc_segment_destroy(s)==0);CHECK(abc_model_release(m)==0);
 puts("C99 pending recovery passed");return 0;
}
