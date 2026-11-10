#include "abcurves.h"
#include <stdio.h>
#define TRY(call) do { status=(call); if(status!=ABC_OK) goto cleanup; } while(0)
int main(int argc,char**argv) {
    abc_model_handle model=0; abc_planner_handle planner=0; abc_segment_handle controller=0;
    abc_status status=ABC_OK; abc_sample samples[32]; uint64_t written=0,i;
    abc_planner_options options={0}; abc_segment_options segment_options={0};
    abc_target_observation obs={0}; abc_segment_metadata meta={0};
    abc_segment_diagnostics diagnostics={0};
    if(argc!=2) return 2;
    TRY(abc_check_abi(1,4));
    TRY(abc_model_load(argv[1],&model));
    /* Null options: seed7/origin0/empty prefix. For a prefix initialize
       abc_planner_options with abc_planner_options_init, then set history/count. */
    TRY(abc_planner_options_init(&options,sizeof(options)));
    TRY(abc_segment_options_init(&segment_options,sizeof(segment_options)));
    TRY(abc_planner_create(model,&options,&planner));
    TRY(abc_segment_create(planner,&segment_options,&controller));
    obs.struct_size=sizeof(obs);obs.kind=ABC_TARGET_RELATIVE;
    obs.availability=ABC_TARGET_AVAILABLE;obs.value.x=120;obs.value.y=-7;
    TRY(abc_segment_submit_observation(controller,&obs));
    meta.struct_size=sizeof(meta);
    TRY(abc_segment_next(controller,samples,32,&written,&meta));
    for(i=0;i<written;++i) {
        printf("%lld %.9f %.9f\n",(long long)samples[i].timestamp_us,samples[i].displacement.x,samples[i].displacement.y);
        if(samples[i].timestamp_us<=meta.start_timestamp_us) {status=ABC_INTERNAL_ERROR;goto cleanup;}
    }
    obs.capture_time_us=obs.receipt_time_us=meta.end_timestamp_us-1000;
    obs.value.x=12;obs.value.y=-6;
    TRY(abc_segment_submit_observation(controller,&obs));
    TRY(abc_segment_next(controller,samples,32,&written,&meta));
    TRY(abc_segment_next(controller,samples,32,&written,&meta)); /* Retained target. */
    diagnostics.struct_size=sizeof(diagnostics);
    TRY(abc_segment_get_diagnostics(controller,&diagnostics));
    printf("%u %llu\n",meta.sample_count,(unsigned long long)diagnostics.total_segments);
cleanup:
    if(status) fprintf(stderr,"%d: %s\n",status,abc_last_error()); /* Copy before cleanup calls. */
    if(controller) abc_segment_destroy(controller);
    if(planner) abc_planner_destroy(planner);
    if(model) abc_model_release(model);
    return status ? 1 : 0;
}
