#include "abcurves/native.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <limits>
#define CHECK(x) do { if(!(x)) throw std::runtime_error(#x); } while(0)
static void same(const abc::Sample& a,const abc::Sample& b) {
 CHECK(a.timestamp_us==b.timestamp_us);CHECK(a.position.x==b.position.x);CHECK(a.position.y==b.position.y);
 CHECK(a.displacement.x==b.displacement.x);CHECK(a.displacement.y==b.displacement.y);
}
int main(int argc,char**argv) { try {
 CHECK(argc==2);
 for(int c:{32,16,8}) for(int prefix:{0,1,2}) {
  auto m=abc::Model::load(std::filesystem::path(argv[1])/(std::to_string(c)+"ms"));
  abc::PlannerOptions options;options.initial_position={100,50};options.seed=11;
  std::array<abc::Vec2,164> raw;for(size_t j=0;j<raw.size();++j)raw[j]={std::sin(double(j)*.12)*.2,std::cos(double(j)*.11)*.1};
  if(prefix==1)options.initial_history.assign(raw.begin(),raw.begin()+160);
  if(prefix==2){auto p=abc::prepare_history(raw,options.initial_position);options.initial_position=p.initial_position;options.initial_history.assign(p.history.begin(),p.history.end());}
  abc::ContinuousPlanner a(m,options),b(m,options);abc::ObservationInput input(a,{abc::ObservationOrdering::caller,50000});
  auto r=input.submit({{12,-7},0,0,abc::ObservationKind::relative});
  CHECK(r.x==options.initial_position.x+12 && r.y==options.initial_position.y-7);
  b.update_target(r,0);
  for(int i=1;i<=2000;++i) {
   same(a.generate_sample(i*1000),b.generate_sample(i*1000));
   if(i%17==0){
    auto before=a.diagnostics();
    auto cap=i*1000-12000;
    auto target=input.submit({{120+i*.1,-7},cap,i*1000+500,abc::ObservationKind::relative});
    b.update_target(target,i*1000+500);
    auto after=a.diagnostics();CHECK(before.motor_evaluations==after.motor_evaluations);
    try{input.submit({{120+i*.1,-7},cap,i*1000+500,abc::ObservationKind::relative});CHECK(false);}catch(const abc::ObservationError&e){CHECK(e.reason==abc::ObservationRejection::duplicate);}
    if(i>50)try{input.submit({{1,2},0,i*1000+500,abc::ObservationKind::absolute});CHECK(false);}catch(const abc::ObservationError&e){CHECK(e.reason==abc::ObservationRejection::stale);}
   }
   if(i==500){input.submit({{},500000,500000,abc::ObservationKind::absolute,abc::TargetAvailability::unavailable});b.clear_target(500000);}
  }
  CHECK(input.diagnostics().accepted>100);
  auto unchanged=a.position();auto revision=a.reset_revision();
  auto invalid=options;invalid.initial_history.resize(4);
  try{a.reinitialize(invalid);CHECK(false);}catch(const std::invalid_argument&){}
  CHECK(a.position().x==unchanged.x && a.reset_revision()==revision);
  a.reinitialize(options);b.reinitialize(options);CHECK(input.diagnostics().accepted==0);
  input.submit({{320,120},0,0});b.update_target({320,120},0);
  std::array<abc::Sample,128> samples;a.advance(128000,samples.data(),samples.size());
  for(int i=1;i<=128;++i)same(samples[i-1],b.generate_sample(i*1000));
 }
 // No allocation or time traversal for very large empty gaps with zero capacity:
 auto m=abc::Model::load(std::filesystem::path(argv[1])/"32ms");abc::ContinuousPlanner p(m);abc::ObservationInput input(p);
 try{input.submit({{1,2},0,std::numeric_limits<int64_t>::max(),abc::ObservationKind::relative});CHECK(false);}catch(const abc::ObservationError&e){CHECK(e.reason==abc::ObservationRejection::timestamp);}
 CHECK(p.diagnostics().numerical_time_us==0);
 std::cout<<"Native observation contract passed: variants, raw/prepared/no prefix, 18000 samples, rejected RNG invariance, reinitialize\n";
 return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;} }
