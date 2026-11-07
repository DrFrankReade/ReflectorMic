
#include "dstar_codec.h"
#include "dstar_rx_loss.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <initializer_list>
extern "C" void sincosf(float x,float*s,float*c){*s=sinf(x);*c=cosf(x);}
int main(){
 char bits[49]{},planes[4][24],reference[49];
 for(unsigned i=0;i<49;++i)bits[i]=(i*7+3)%2;
 bits[0]=1;bits[1]=0;bits[2]=1;bits[3]=0;bits[4]=0;bits[5]=0;bits[48]=0;
 assert(mbe_encodeAmbe3600x2400Frame(bits,planes)>=0);
 mbe_process_result result{};
 assert(mbe_decodeAmbe3600x2400Frame(planes,reference,&result)>=0);
 DStarCodec codec;assert(codec.begin());
 uint8_t wire[9];int16_t pcm[160],baseline[160];
 assert(mbe_encodeDStarDVData(planes,wire)>=0);
 codec.resetDecoder();assert(codec.decode(wire,baseline));
 unsigned corrected=0;
 auto check=[&](int plane,int first,int count){
  char damaged[4][24],decoded[49];memcpy(damaged,planes,sizeof planes);
  for(int k=0;k<count;++k)damaged[plane][first+k]^=1;
  assert(mbe_encodeDStarDVData(damaged,wire)>=0);
  char unpacked[4][24];assert(mbe_decodeDStarDVData(wire,unpacked)>=0);
  assert(mbe_decodeAmbe3600x2400Frame(unpacked,decoded,&result)>=0);
  assert(!memcmp(reference,decoded,49));assert(result.total_errors>=0 && result.total_errors<=count);
  codec.resetDecoder();assert(codec.decode(wire,pcm,&result));
  assert(result.total_errors>=0 && result.total_errors<=count);assert(!memcmp(baseline,pcm,sizeof pcm));++corrected;
 };
 for(int k=0;k<24;++k)check(0,k,1);
 for(int k=0;k<23;++k)check(1,k,1);
 check(0,1,3);check(1,0,3);
 printf("FEC: %u protected-bit corruption cases restored identical bits and PCM through actual wrapper\n",corrected);
 const uint8_t nullAmbe[9]={0x9e,0x8d,0x32,0x88,0x26,0x1a,0x3f,0x61,0xe8};
 codec.resetDecoder();assert(codec.decode(nullAmbe,pcm,&result));
 assert(!(result.flags & (MBE_PROCESS_FLAG_ERASURE|MBE_PROCESS_FLAG_REPEAT)));
 printf("Received null AMBE remains silence, not an erasure\n");
 memset(bits,0,sizeof bits);for(int k=0;k<6;++k)bits[k]=(62>>(5-k))&1;bits[48]=1;
 assert(mbe_encodeAmbe3600x2400Frame(bits,planes)>=0);
 assert(mbe_encodeDStarDVData(planes,wire)>=0);
 assert(!memcmp(wire,dstar::RX_LOST_AMBE,9));
 codec.resetDecoder();
 for(unsigned i=1;i<=5;++i){
  assert(codec.decode(wire,pcm,&result));
  assert(result.flags & MBE_PROCESS_FLAG_ERASURE);assert(result.flags & MBE_PROCESS_FLAG_REPEAT);
  assert(bool(result.flags & MBE_PROCESS_FLAG_MUTE)==(i>=4));
 }
 puts("Local loss marker: repeat first 3 losses; mute from fourth loss passed");

 DStarCodec sender;
 assert(sender.begin());
 for(unsigned losses : {1U,2U,3U,4U,8U,30U}) {
  codec.resetDecoder(); sender.resetEncoder();
  uint8_t speech[9];
  for(unsigned frame=0;frame<30;++frame) {
   int16_t input[160];
   for(unsigned i=0;i<160;++i) {
    const double t=(frame*160+i)/8000.0;
    input[i]=int16_t(2000*std::sin(2*3.141592653589793*110*t)+
                      600*std::sin(2*3.141592653589793*330*t));
   }
   assert(sender.encode(input,speech));assert(codec.decode(speech,pcm,&result));
  }
  for(unsigned i=1;i<=losses;++i) {
   assert(codec.decode(dstar::RX_LOST_AMBE,pcm,&result));
   assert(result.flags & MBE_PROCESS_FLAG_ERASURE);
   assert(result.flags & MBE_PROCESS_FLAG_REPEAT);
   assert(bool(result.flags & MBE_PROCESS_FLAG_MUTE)==(i>=4));
  }
  // Resume without resetting prediction; the library owns the fade and state.
  long long energy=0;
  for(unsigned frame=0;frame<20;++frame) {
   assert(codec.decode(speech,pcm,&result));
   assert(!(result.flags & (MBE_PROCESS_FLAG_ERASURE|MBE_PROCESS_FLAG_REPEAT|MBE_PROCESS_FLAG_MUTE)));
   for(auto x:pcm) energy+=int(x)*int(x);
  }
  assert(energy>0);
 }
 puts("Voiced loss bursts 20-600 ms: conceal, fade/mute and resume without decoder reset passed");
}
