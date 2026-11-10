// Test-only raw C99 state trace; does not expose private state in the public ABI.
#include "abc_online.h"
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>
int main(int argc,char**argv) try {
 if(argc!=5)return 2;
 std::ifstream blob(argv[1],std::ios::binary|std::ios::ate);auto bytes=blob.tellg();
 if(bytes<=0)throw std::runtime_error("missing model");std::vector<char> data(size_t(bytes),0);blob.seekg(0);blob.read(data.data(),bytes);
 abc_online_model_t m{};if(abc_online_model_init(&m,data.data(),data.size()))throw std::runtime_error("bad model");
 std::ifstream a(argv[2]),b(argv[3]);size_t na,nb;double sa,sb;uint64_t ea,eb;a>>na>>sa>>ea;b>>nb>>sb>>eb;
 if(na!=nb||sa!=sb||ea!=eb)throw std::runtime_error("fixture mismatch");
 abc_online_renderer_t ra{},rb{};abc_online_reset(&ra,&m);abc_online_reset(&rb,&m);
 for(int i=0;i<256;i++){int x,y,u,v;a>>x>>y;b>>u>>v;if(x!=u||y!=v)throw std::runtime_error("profile mismatch");abc_online_observe_raw(&ra,int16_t(x),int16_t(y));abc_online_observe_raw(&rb,int16_t(u),int16_t(v));}
 abc_online_begin(&ra,ea);abc_online_begin(&rb,eb);
 std::ofstream out(argv[4]);out<<"i,px,py,nx,ny,pqx,pqy,nqx,nqy,pbeforex,pbeforey,nbeforex,nbeforey,pafterx,paftery,nafterx,naftery,prx,pry,nrx,nry,hidden_equal,cumx,cumy\n"<<std::setprecision(17);
 long long cx=0,cy=0;
 for(size_t i=0;i<na;i++){
 double x,y,u,v;a>>x>>y;b>>u>>v;auto q=[](double z){return int32_t(std::nearbyint(double(float(z))*65536.));};
 auto qx=q(x/sa),qy=q(y/sa),qu=q(u/sb),qv=q(v/sb);auto pa=ra.fixed.accumulator_q16[0],pb=ra.fixed.accumulator_q16[1],nc=rb.fixed.accumulator_q16[0],nd=rb.fixed.accumulator_q16[1];
 abc_fixed_report_t p{},n{};if(abc_online_step(&ra,qx,qy,&p)||abc_online_step(&rb,qu,qv,&n))throw std::runtime_error("step failed");
 bool same=true;for(int j=0;j<ABC_FIXED_HIDDEN;j++)same&=ra.fixed.hidden[j]==rb.fixed.hidden[j];cx+=n.dx-p.dx;cy+=n.dy-p.dy;
 out<<i<<','<<x<<','<<y<<','<<u<<','<<v<<','<<qx<<','<<qy<<','<<qu<<','<<qv<<','<<pa<<','<<pb<<','<<nc<<','<<nd<<','<<ra.fixed.accumulator_q16[0]<<','<<ra.fixed.accumulator_q16[1]<<','<<rb.fixed.accumulator_q16[0]<<','<<rb.fixed.accumulator_q16[1]<<','<<p.dx<<','<<p.dy<<','<<n.dx<<','<<n.dy<<','<<same<<','<<cx<<','<<cy<<'\n';
 }
 return a&&b?0:1;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
