// Non-production measurement driver; calls the native engine directly.
#include <abcurves/native.hpp>
#include "numerical.hpp"
#include "matvec.hpp"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>
using Clock=std::chrono::steady_clock;
double sink=0;
template<class F> void measure(std::ostream& out,const char* op,int count,F f) {
  const std::string name=op;
  const int batch=(name=="target_update"||name=="decode_selected"||name=="prepare_history")?16:1;
  for(int i=0;i<32;++i) f(i);
  for(int i=0;i<count;++i){auto b=Clock::now();for(int j=0;j<batch;++j)f(i);auto e=Clock::now();
    out<<op<<','<<std::chrono::duration<double,std::micro>(e-b).count()/batch<<'\n';}
}
int main(int argc,char**argv) try {
  if(argc!=5)return 2;
  const std::filesystem::path root=argv[1];int c=std::stoi(argv[2]);
  const auto dir=root/"lib/models"/(std::to_string(c)+"ms");
  std::ofstream out(argv[3]);out<<std::setprecision(17)<<"operation,us\n";
  auto model=abc::Model::load(dir);abc::ContinuousPlanner retained(model);
  measure(out,"model_load",20,[&](int){auto m=abc::Model::load(dir);sink+=m->commitment_ms();});
  measure(out,"initialize_loaded",200,[&](int){abc::ContinuousPlanner p(model);sink+=p.position().x;});
  abc::ContinuousPlanner p(model);
  measure(out,"reset_session",1000,[&](int){p.reset(7);});
  long long receipt=0;
  measure(out,"target_update",1000,[&](int i){p.update_target({320.+i*.01,120.},receipt++);});
  abc::PlannerOptions active_options;active_options.initial_history.assign(160,{.1,.05});
  abc::ContinuousPlanner active(model,active_options);
  auto generate=[&](const char*label,int n){
    for(int i=-32;i<n;++i){active.reset(7);active.update_target({320.,120.},0);abc::Sample sample[32];
      auto b=Clock::now();auto count=active.advance(label[0]=='f'?1000:c*1000,sample,32);auto e=Clock::now();
      if(active.diagnostics().motor_evaluations!=1)throw std::runtime_error("fresh workload did not evaluate motor");
      sink+=sample[count-1].position.x;
      if(i>=0)out<<label<<','<<std::chrono::duration<double,std::micro>(e-b).count()<<'\n';}}
  ;generate("fresh_decision",400);generate("complete_commitment",400);
  p.reset(7);p.update_target({320.,120.},0);long long now=0;abc::Sample sample;
  for(int i=-32;i<1200;++i){while(p.diagnostics().pending_samples<4){p.advance(now+1000,&sample,1);now+=1000;}
    auto b=Clock::now();for(int j=0;j<4;++j){p.advance(now+1000,&sample,1);now+=1000;}auto e=Clock::now();
    if(i>=0)out<<"buffered_sample,"<<std::chrono::duration<double,std::micro>(e-b).count()/4<<'\n';}
  std::array<abc::Vec2,640> h,g;std::array<std::int64_t,640>a;std::array<std::uint8_t,640>v,m;
  for(int i=0;i<640;++i){h[i]={.8*std::sin(i*.017),.4*std::cos(i*.027)};g[i]={80.+.06*i,-40.+.03*i};a[i]=9000000+(i-639)*1000;v[i]=m[i]=1;}
  abc::detail::Observation ob{h.data(),g.data(),a.data(),v.data(),m.data(),{23.,-7.},9000000};
  auto weights=std::make_shared<abc::detail::Weights>(*model);abc::detail::Neural n(weights);
  auto f=abc::detail::motor_features(ob);abc::detail::EventState es;es.initial_error=80.;es.target={40.,-25.};es.position={12.,-3.};es.mode=0;
  auto ef=abc::detail::event_features(ob,es);
  measure(out,"motor_features",1000,[&](int){f=abc::detail::motor_features(ob);sink+=f.coarse[2];});
  measure(out,"event_features",1000,[&](int){ef=abc::detail::event_features(ob,es);sink+=ef.raw[0];});
  measure(out,"motor_inference",500,[&](int){n.motor(f);sink+=n.encoded[0];});
  std::array<float,32>previous{};
  measure(out,"selector_geometry_network",1000,[&](int){n.geometry(h.back());n.selector(previous,false,{3.7,-1.2},c);sink+=n.logits[0];});
  measure(out,"hazard_inference",1000,[&](int){n.hazards(ef.context);sink+=n.hazard[0];});
  measure(out,"brake_inference",1000,[&](int){n.brake(ef.context);sink+=n.duration[0];});
  abc::Vec2 action[32];measure(out,"decode_selected",1000,[&](int){n.decode(3,h.back(),c,action);sink+=action[0].x;});
  std::array<abc::Vec2,5>brake_coeff{};for(int j=0;j<5;++j)brake_coeff[j]={.02*j,-.01*j};
  measure(out,"brake_path",1000,[&](int){for(int j=0;j<=c;++j){auto q=abc::detail::c2_path(h.back(),{.04,-.02},64.,brake_coeff,j+7.);sink+=q.x;}});
  std::array<abc::Vec2,164>raw;for(int i=0;i<164;++i)raw[i]={.3*std::sin(i*.03),.2*std::cos(i*.04)};
  measure(out,"prepare_history",1000,[&](int){auto x=abc::prepare_history(raw,{23.,-7.});sink+=x.initial_position.x;});
  auto prepared=abc::prepare_history(raw,{23.,-7.});
  std::ofstream prefix(std::string(argv[3])+".prefix.csv");prefix<<std::setprecision(17)<<prepared.initial_position.x<<','<<prepared.initial_position.y<<'\n';
  for(auto x:prepared.history)prefix<<x.x<<','<<x.y<<'\n';
  auto renderer_path=root/"Native/models/renderer_global_h80.bin";
  measure(out,"renderer_load",20,[&](int){abc::Renderer x(renderer_path);sink+=x.ready();});
  abc::Renderer renderer(renderer_path);std::vector<abc::Report>profile(256);
  for(int i=0;i<256;++i)profile[i]={std::int16_t(i%9-4),std::int16_t(i%7-3)};
  measure(out,"renderer_profile",200,[&](int){renderer.observe_profile(profile);});
  for(int i=-32;i<200;++i){renderer.observe_profile(profile);auto b=Clock::now();renderer.begin(7);auto e=Clock::now();
    if(i>=0)out<<"renderer_begin,"<<std::chrono::duration<double,std::micro>(e-b).count()<<'\n';}
  std::array<abc::Vec2,1200>intents;for(int i=0;i<1200;++i)intents[i]={.7*std::sin(i*.013),.4*std::cos(i*.017)};
  renderer.observe_profile(profile);renderer.begin(7);measure(out,"renderer_step",1200,[&](int i){auto x=renderer.step(intents[i]);sink+=x.dx;});
  // Same-input Renderer replay for parity, independent of planner differences.
  std::ifstream input(argv[4]);int count;input>>count;renderer.observe_profile(profile);renderer.begin(7);
  std::ofstream reports(std::string(argv[3])+".reports.csv");reports<<"dx,dy\n";
  for(int i=0;i<count;++i){abc::Vec2 x;input>>x.x>>x.y;auto report=renderer.step(x);reports<<report.dx<<','<<report.dy<<'\n';}
  std::ofstream meta(std::string(argv[3])+".json");meta<<"{\"kernel\":\""<<abc::detail::kernel_name()<<"\",\"sink\":"<<sink<<"}";
  return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
