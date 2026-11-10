"""Numerical example; application controls real clock, conversion and transport."""
import sys
from abcurves_native import Model, Planner, SegmentController, SampleBuffer

with Model(sys.argv[1]) as model, Planner(model) as planner, SegmentController(planner) as controller:
    # Optional history=160 prepared displacement pairs at Planner construction.
    # Raw164: h=prepare_history(raw,current_position=(x,y));
    # Planner(model,initial_position=h.initial_position,history=h.history)
    output = SampleBuffer(controller.required_capacity())
    controller.relative((120., -7.), 0, 0)
    first = controller.next_segment_into(output)
    for sample in output:
        print(sample.timestamp_us, sample.displacement.x, sample.displacement.y)
        assert sample.timestamp_us > first.start_timestamp_us
    receipt = first.end_timestamp_us - 1000  # Simulated arrival during commitment.
    controller.relative((12., -6.), receipt, receipt)
    second = controller.next_segment_into(output)  # Reuse only after consumption.
    third = controller.next_segment_into(output)   # No new target required.
    diagnostics = controller.diagnostics
    assert diagnostics.total_segments == 3
    print(third.sample_count, diagnostics.total_segments)
