import argparse
from abcurves_native import Model,Planner,SampleBuffer
p=argparse.ArgumentParser();p.add_argument('model');a=p.parse_args()
with Model(a.model) as model,Planner(model,seed=7) as planner:
    planner.update_target((320,120),0)
    samples=SampleBuffer(128)
    planner.advance_into(128000,samples)
    print(len(samples),tuple(samples[-1].position),planner.state.mode)
