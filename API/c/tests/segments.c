#include "abcurves.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s (%s)\n",__LINE__,#x,abc_last_error());exit(1);}}while(0)
int main(int argc,char**argv){
 abc_model_handle model;abc_planner_handle planner;abc_segment_handle segments;abc_sample samples[32];uint64_t n;
 abc_segment_options options;abc_segment_metadata m;abc_segment_diagnostics d;abc_target_observation o;
 uint32_t a,b,c;CHECK(argc==2);CHECK(abc_check_abi(1,0)==0 && abc_check_abi(1,1)==0 && abc_check_abi(1,2)==0);
 CHECK(abc_segment_layout(&a,&b,&c)==0);CHECK(a==sizeof(options)&&b==sizeof(m)&&c==sizeof(d));
 CHECK(abc_model_load(argv[1],&model)==0);CHECK(abc_planner_create(model,0,&planner)==0);
 CHECK(abc_segment_options_init(&options,sizeof(options))==0);CHECK(options.inactivity_timeout_us==-1 && options.ordering==1);
 CHECK(abc_segment_create(planner,&options,&segments)==0);memset(&m,0,sizeof(m));m.struct_size=sizeof(m);
 CHECK(abc_segment_get_metadata(segments,&m)==ABC_INVALID_STATE);
 memset(&o,0,sizeof(o));o.struct_size=sizeof(o);o.kind=1;o.value.x=120;o.value.y=-7;
 CHECK(abc_segment_submit_observation(segments,&o)==0);
 CHECK(abc_segment_next(segments,samples,0,&n,&m)==ABC_INSUFFICIENT_CAPACITY && n==0);
 CHECK(abc_segment_next(segments,samples,32,&n,&m)==0);CHECK(n==m.variant_ms && n==m.sample_count && m.start_timestamp_us==0 && m.end_timestamp_us==(int64_t)n*1000);
 o.capture_time_us=7000;o.receipt_time_us=7000;o.value.x=12;
 CHECK(abc_segment_submit_observation(segments,&o)==0);CHECK(abc_segment_submit_observation(segments,&o)==ABC_DUPLICATE_OBSERVATION);
 CHECK(abc_segment_next(segments,samples,32,&n,&m)==0);
 d.struct_size=sizeof(d);CHECK(abc_segment_get_diagnostics(segments,&d)==0);CHECK(d.duplicate_observations==1 && d.total_segments==2);
 CHECK(abc_segment_reset_diagnostics(segments)==0);CHECK(abc_segment_get_diagnostics(segments,&d)==0 && d.total_segments==0);
 CHECK(abc_segment_reset_session(segments)==0);CHECK(abc_segment_next(segments,samples,32,&n,&m)==0 && n==1 && m.start_timestamp_us==0);
 CHECK(abc_planner_destroy(planner)==0);CHECK(abc_segment_get_diagnostics(segments,&d)==ABC_INVALID_HANDLE);
 CHECK(abc_segment_destroy(segments)==0);CHECK(abc_model_release(model)==0);
 puts("C99 segment contract passed");return 0;
}
