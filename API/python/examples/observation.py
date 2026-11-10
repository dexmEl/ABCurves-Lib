"""Independent virtual target input, with capture metadata and no device dependency."""
import sys
from abcurves_native import Model,Planner,ObservationInput,SampleBuffer
with Model(sys.argv[1]) as model,Planner(model) as planner,ObservationInput(planner) as observations:
    output=SampleBuffer(128)
    planner.advance_into(12000,output) # application consumes these samples
    target=observations.relative((12,-7),capture_time_us=0,receipt_time_us=12000)
    print('absolute virtual target:',tuple(target))
    planner.advance_into(24000,output)
    print('accepted:',observations.diagnostics.accepted)
