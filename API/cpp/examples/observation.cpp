#include "abcurves/native.hpp"
#include <array>
#include <iostream>
int main(int argc,char**argv)try{
 if(argc!=2)return 2;
 abc::ContinuousPlanner planner(abc::Model::load(argv[1]));
 abc::ObservationInput input(planner);
 std::array<abc::Sample,128> output;
 // Collect elapsed virtual samples before applying the newly available detection.
 auto count=planner.advance(12000,output.data(),output.size());
 auto target=input.submit({{12,-7},0,12000,abc::ObservationKind::relative});
 std::cout<<count<<" samples, target "<<target.x<<','<<target.y<<'\n';
 planner.advance(24000,output.data(),output.size());
 // Render or send output elsewhere. No execution confirmation enters the planner.
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
