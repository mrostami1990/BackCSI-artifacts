#include "backcsi/backcsi.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace backcsi {
namespace {
constexpr double pi=3.1415926535897932384626433832795;
using P=std::array<double,5>;
void valid(const CSI& x){for(auto z:x)if(!std::isfinite(z.real())||!std::isfinite(z.imag()))throw std::invalid_argument("CSI must contain 51 finite complex samples");}
double norm2(const CSI& x){double n=0;for(auto z:x)n+=std::norm(z);return n;}
double rms(const CSI& x){return std::sqrt(norm2(x)/51.0);}
Complex dot(const CSI& x,const CSI& y){Complex z=0;for(int i=0;i<51;i++)z+=std::conj(x[i])*y[i];return z;}
struct Aligned {CSI a,b;double beta;};
Aligned align(const CSI& c1,const CSI& c2,const P& p){
 Complex g1(p[0],p[1]),g2(p[2],p[3]);
 CSI a=deconjugate(c1,g1),u2=deconjugate(c2,g2),shifted{};
 const auto& k=subcarriers();
 for(int i=0;i<51;i++)shifted[i]=u2[i]*std::polar(1.0,-p[4]*k[i]);
 double beta=std::arg(dot(a,shifted));
 for(auto& z:shifted)z*=std::polar(1.0,-beta);
 return {a,shifted,beta};
}
double loss(const CSI& c1,const CSI& c2,const P& p){
 Complex g1(p[0],p[1]),g2(p[2],p[3]);
 double n1=std::norm(g1),n2=std::norm(g2);
 if(n1>0.95*0.95 || n2>0.95*0.95)return std::numeric_limits<double>::infinity();
 double f1=1.0/(1.0-n1),f2=1.0/(1.0-n2);
 Complex rot=std::polar(1.0,-p[4]),z=std::polar(1.0,26*p[4]);
 Complex inner{};double aa=0,bb=0;
 for(int i=0;i<51;i++){
  if(i==25)z=rot;
  Complex a=(c1[i]-g1*std::conj(c1[i]))*f1;
  Complex b=(c2[i]-g2*std::conj(c2[i]))*f2;
  inner+=std::conj(a)*b*z;
  aa+=std::norm(a);bb+=std::norm(b);
  z*=rot;
 }
 return std::max(0.0,aa+bb-2.0*std::abs(inner));
}
std::array<Complex,64> lltf_spectrum(){
 constexpr int negative[26]={1,1,-1,-1,1,1,-1,1,-1,1,1,1,1,1,1,-1,-1,1,1,-1,1,-1,1,1,1,1};
 constexpr int positive[26]={1,-1,-1,1,1,-1,1,-1,1,-1,-1,-1,-1,-1,1,1,-1,-1,1,-1,1,-1,1,1,1,1};
 std::array<Complex,64> out{};for(int i=0;i<26;i++){out[1+i]=positive[i];out[38+i]=negative[i];}return out;
}
std::array<Complex,64> dft64(const std::array<Complex,64>& x,bool inverse){
 std::array<Complex,64> y{};for(int k=0;k<64;k++){
 Complex s=0;for(int n=0;n<64;n++)s+=x[n]*std::polar(1.0,(inverse?1.0:-1.0)*2*pi*k*n/64.0);
 y[k]=inverse?s/64.0:s;
 }return y;
}
} // anonymous
const std::array<double,51>& subcarriers(){static const std::array<double,51> k=[](){std::array<double,51>a{};for(int i=0;i<25;i++)a[i]=i-26;for(int i=25;i<51;i++)a[i]=i-24;return a;}();return k;}
const std::vector<int>& alphabet(int order){
 static const std::vector<int> a2={5,95},a4={5,29,50,95},a8={5,29,39,50,59,70,79,95},a16={5,11,17,23,29,34,39,45,50,54,59,70,76,79,87,95};
 switch(order){case 2:return a2;case 4:return a4;case 8:return a8;case 16:return a16;default:throw std::invalid_argument("order must be 2,4,8,16");}
}
CSI deconjugate(const CSI& x,Complex g){double n=std::norm(g);if(n>=1)throw std::invalid_argument("|gamma| must be < 1");CSI y{};for(int i=0;i<51;i++)y[i]=(x[i]-g*std::conj(x[i]))/(1-n);return y;}
Cancellation cancel(const CSI& c1, const CSI& c2, int max_iterations, double gamma_max) {
 valid(c1); valid(c2);
 if(max_iterations<0) throw std::invalid_argument("negative iterations");
 if(gamma_max<=0 || gamma_max>0.95) throw std::invalid_argument("gamma_max must be in (0,0.95]");
 P p{}, steps={0.01,0.01,0.01,0.01,0.005};
 double best=loss(c1,c2,p);
 // Locally refine each complex conjugate coefficient, then the phase slope.
 // Halve grid spacing when a complete sweep does not improve the residual.
 for(int it=1;it<=max_iterations;it++) {
  bool improved=false;
  for(int base : {0,2}) {
   P next=p; double next_loss=best;
   for(int dx=-1;dx<=1;dx++) for(int dy=-1;dy<=1;dy++) {
    if(dx==0 && dy==0) continue;
    P q=p; q[base]+=dx*steps[base]; q[base+1]+=dy*steps[base+1];
    if(std::hypot(q[base],q[base+1])>gamma_max) continue;
    double score=loss(c1,c2,q);
    if(score<next_loss) {next_loss=score; next=q;}
   }
   if(next_loss<best) {best=next_loss; p=next; improved=true;}
  }
  P next=p; double next_loss=best;
  for(int sign : {-1,1}) {
   P q=p; q[4]+=sign*steps[4]; double score=loss(c1,c2,q);
   if(score<next_loss) {next_loss=score;next=q;}
  }
  if(next_loss<best) {best=next_loss;p=next;improved=true;}
  if(!improved) {
   for(double& step:steps) step*=0.5;
   if(*std::max_element(steps.begin(),steps.end())<1e-6) break;
  }
 }
 auto v=align(c1,c2,p);
 Cancellation out;
 out.channel_reference=v.a; out.channel_data=v.b;
 out.delta_beta=v.beta; out.delta_alpha=p[4];
 out.gamma_reference={p[0],p[1]}; out.gamma_data={p[2],p[3]};
 CSI rawdiff{};
 for(int i=0;i<51;i++) {
  out.residual[i]=v.b[i]-v.a[i]; rawdiff[i]=c2[i]-c1[i];
 }
 out.gain_db=20*std::log10((rms(rawdiff)+1e-12)/(rms(out.residual)+1e-12));
 return out;
}
CSI extract_51(const std::array<Complex,64>& x){CSI y{};for(int i=0;i<25;i++)y[i]=x[38+i];for(int i=0;i<26;i++)y[25+i]=x[1+i];return y;}
CSI parse_nexmon_udp(const std::vector<std::uint8_t>& bytes,std::size_t header_bytes){if(bytes.size()!=header_bytes+256)throw std::invalid_argument("expected header + 256 bytes for 64 interleaved int16 IQ");std::array<Complex,64>x{};for(int i=0;i<64;i++){auto rd=[&](std::size_t off){uint16_t u=uint16_t(bytes[off])|(uint16_t(bytes[off+1])<<8);return static_cast<std::int16_t>(u);};auto j=header_bytes+4*i;x[i]={double(rd(j)),double(rd(j+2))};}return extract_51(x);}
CSI template_for_duty(int duty){if(duty<=0||duty>=100)throw std::invalid_argument("duty must lie in (0,100)");auto x=dft64(lltf_spectrum(),true);
 double power=0;for(auto z:x)power+=std::norm(z);double normalization=std::sqrt(power/64.0);
 for(int i=0;i<64;i++){double pulse=((i+0.5)/64.0<duty/100.0)?1.0:-1.0;x[i]=x[i]/normalization*pulse;}
 return extract_51(dft64(x,false));}
Decision decode_residual(const CSI& y,int order,int slope_samples,double amin,double amax){
 valid(y);const auto& duties=alphabet(order);if(slope_samples<1||amin>amax)throw std::invalid_argument("invalid slope grid");
 Decision out;out.scores.resize(duties.size());double best=-1;double yn=std::sqrt(norm2(y));const auto& k=subcarriers();
 // Cache DFT templates by duty: avoids computing them inside the slope search.
 for(std::size_t d=0;d<duties.size();d++){
 static const std::array<CSI,16> cached=[](){std::array<CSI,16> ts{};auto& ds=alphabet(16);for(std::size_t j=0;j<ds.size();j++)ts[j]=template_for_duty(ds[j]);return ts;}();
 const auto& full=alphabet(16);auto it=std::find(full.begin(),full.end(),duties[d]);const CSI& t=cached[static_cast<std::size_t>(it-full.begin())];double tn=std::sqrt(norm2(t));double top=-1,topa=0,topb=0;
 for(int j=0;j<slope_samples;j++){
 double a=slope_samples==1?amin:amin+(amax-amin)*j/(slope_samples-1);
 Complex inner=0;
 Complex rot=std::polar(1.0,-a),z=std::polar(1.0,26*a);
 for(int i=0;i<51;i++) {
  if(i==25) z=rot;
  inner+=y[i]*z*std::conj(t[i]); z*=rot;
 }
 double score=std::abs(inner)/(yn*tn+1e-15);
 if(score>top){top=score;topa=a;topb=std::arg(inner);}
 }
 out.scores[d]=top;if(top>best){best=top;out.symbol=static_cast<int>(d);out.duty_percent=duties[d];out.slope=topa;out.intercept=topb;}
 }
 for(int i=0;i<51;i++)out.deramped[i]=y[i]*std::polar(1.0,-(out.slope*k[i]+out.intercept));
 return out;
}
Result decode_pair(const CSI& ref,const CSI& data,int order,int max_iterations){Result r;r.cancellation=cancel(ref,data,max_iterations);r.decision=decode_residual(r.cancellation.residual,order);return r;}
}
