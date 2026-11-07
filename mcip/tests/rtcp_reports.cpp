// SPDX-License-Identifier: MPL-2.0
#include <rtc/rtcpreceivingsession.hpp>
#include <rtc/rtp.hpp>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <thread>
#include <atomic>
using namespace rtc;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) throw std::runtime_error(std::string(#x)+" line "+std::to_string(__LINE__)); } while(0)
static uint32_t word(const Message &m, size_t n) { CHECK(n+4<=m.size()); return (std::to_integer<uint32_t>(m[n])<<24)|(std::to_integer<uint32_t>(m[n+1])<<16)|(std::to_integer<uint32_t>(m[n+2])<<8)|std::to_integer<uint32_t>(m[n+3]); }
static void put(Message &m,size_t p,uint32_t n) { for(int i=3;i>=0;--i){m[p+i]=byte(n&255);n>>=8;} }
static message_ptr rtp(uint16_t seq,uint32_t ts=0,SSRC ssrc=0x12345678) {
 auto m=make_message(13);(*m)[0]=byte(0x80);(*m)[1]=byte(96);(*m)[2]=byte(seq>>8);(*m)[3]=byte(seq);put(*m,4,ts);put(*m,8,ssrc);return m;
}
static message_ptr sr(SSRC ssrc=0x12345678) {auto m=make_message(28,Message::Control);(*m)[0]=byte(0x80);(*m)[1]=byte(200);(*m)[3]=byte(6);put(*m,4,ssrc);put(*m,8,0x12345678);put(*m,12,0x9abcdef0);return m;}
struct Receiver:RtcpReceivingSession {
 using RtcpReceivingSession::RtcpReceivingSession;
 using RtcpReceivingSession::incomingAt;using RtcpReceivingSession::pushRRAt;
 static auto at(int64_t ms) { return std::chrono::steady_clock::time_point(std::chrono::milliseconds(ms)); }
 unsigned feed(message_ptr m,int64_t ms=0) {message_vector v{std::move(m)};unsigned replies=0;incomingAt(v,[&](auto){++replies;},at(ms));return replies;}
 message_ptr report(int64_t ms=0) {message_ptr r;pushRRAt([&](auto p){r=std::move(p);},at(ms));return r;}
};
static int32_t loss(const Message &m) {const uint32_t x=word(m,12)&0xffffff;return x&0x800000?int32_t(int64_t(x)-0x1000000):int32_t(x);}
static void active(Receiver &r) {r.feed(rtp(100));r.feed(rtp(101));}
int main() {
 try {
  {Receiver r;CHECK(r.report()->size()==8);r.feed(rtp(100));CHECK(r.report()->size()==8);r.feed(rtp(101));r.feed(rtp(102));auto rr=r.report();CHECK(rr->size()==32);CHECK(word(*rr,4)!=0x12345678);CHECK(word(*rr,8)==0x12345678);CHECK(word(*rr,16)==102);CHECK(loss(*rr)==0);CHECK(word(*rr,24)==0&&word(*rr,28)==0);}
  {Receiver r;for(auto n:{65534,65535,0,1})r.feed(rtp(n));auto rr=r.report();CHECK(word(*rr,16)==65537);CHECK(loss(*rr)==0);}
  {Receiver r;r.feed(rtp(65535));r.feed(rtp(0));CHECK(r.report()->size()==32);CHECK(word(*r.report(),16)==0);}
  {Receiver r;active(r);r.feed(rtp(103));auto rr=r.report();CHECK(loss(*rr)==1);CHECK((word(*rr,12)>>24)==85);CHECK((word(*r.report(),12)>>24)==0);r.feed(rtp(102));CHECK(loss(*r.report())==0);r.feed(rtp(103));rr=r.report();CHECK(loss(*rr)==-1);CHECK((word(*rr,12)>>24)==0);}
  {Receiver r;active(r);r.feed(sr(),10);r.feed(rtp(5000));CHECK(word(*r.report(),16)==101);r.feed(rtp(5001));auto rr=r.report();CHECK(word(*rr,16)==5001&&loss(*rr)==0);CHECK(word(*rr,24)==0&&word(*rr,28)==0);}
  {Receiver r;active(r);r.feed(sr(),100);auto rr=r.report(1100);CHECK(word(*rr,24)==0x56789abc);CHECK(word(*rr,28)==65536);r.feed(rtp(700,0,42));CHECK(r.report()->size()==8);r.feed(rtp(701,0,42));rr=r.report();CHECK(word(*rr,8)==42&&word(*rr,16)==701);CHECK(word(*rr,24)==0);CHECK(r.feed(sr(0x12345678))==0);CHECK(word(*r.report(),8)==42);}
  {Receiver r;r.feed(sr(),0);CHECK(r.report()->size()==8);active(r);CHECK(word(*r.report(1000),28)==65536);}
  {Receiver r;Description::Audio audio("audio");audio.addOpusCodec(96);r.media(audio);r.feed(rtp(100,0),0);r.feed(rtp(101,960),20);r.feed(rtp(102,1920),50);CHECK(word(*r.report(),20)==30);Description::Video video("video");video.addH264Codec(96);r.media(video);r.feed(rtp(103,3000),60);CHECK(word(*r.report(),20)==0);r.feed(rtp(104,3900),70);CHECK(word(*r.report(),20)==0);}
  {Receiver r(48000);r.feed(rtp(100,0),0);r.feed(rtp(101,960),20);r.feed(rtp(102,1920),50);CHECK(word(*r.report(),20)==30);}
  {Receiver r(90000);r.feed(rtp(100,0xffffff00),0);r.feed(rtp(101,0xffffff80),1);r.feed(rtp(102,0x20),3);CHECK(word(*r.report(),20)==1);}
  {Receiver r;active(r);r.feed(rtp(102,1000000),100000);CHECK(word(*r.report(),20)==0);}
  {RtcpReportBlock b{};b.prepareReceptionReport(7,128,-1,0x12345678,5,0x123456789abcdef0ULL,65536);CHECK(b.getFractionLost()==128);CHECK(b.getPacketsLostCountSigned()==-1);CHECK(b.extendedHighestSeqNo()==0x12345678);b.prepareReceptionReport(7,255,INT32_MAX,0,0,0,0);CHECK(b.getPacketsLostCountSigned()==8388607);b.prepareReceptionReport(7,0,INT32_MIN,0,0,0,0);CHECK(b.getPacketsLostCountSigned()==-8388608);}
  // Reject malformed RTCP before reading bodies; no callbacks or state changes.
  {Receiver r;active(r);auto before=r.report();
   for(size_t len=0;len<28;++len){auto m=sr();m->resize(len);CHECK(r.feed(m)==0);}
   auto m=sr();(*m)[0]=byte(0x81);CHECK(r.feed(m)==0);m=sr();(*m)[0]=byte(0x40);CHECK(r.feed(m)==0);
   m=sr();m->push_back(byte(0));CHECK(r.feed(m)==0);
   m=sr();(*m)[0]=byte(0xa0);(*m)[27]=byte(0);CHECK(r.feed(m)==0);
   m=sr();(*m)[0]=byte(0xa0);(*m)[27]=byte(29);CHECK(r.feed(m)==0);
   CHECK(word(*r.report(),16)==word(*before,16));CHECK(word(*r.report(),24)==0);
   auto compound=sr();auto sdes=make_message(4,Message::Control);(*sdes)[0]=byte(0x80);(*sdes)[1]=byte(202);compound->insert(compound->end(),sdes->begin(),sdes->end());CHECK(r.feed(compound)==1);
  }
  {Receiver r;active(r);auto m=rtp(500);(*m)[0]=byte(0x8f);CHECK(r.feed(m)==0);m=rtp(500);(*m)[0]=byte(0x90);r.feed(m);m=rtp(500);(*m)[0]=byte(0xa0);(*m)[12]=byte(2);r.feed(m);CHECK(word(*r.report(),16)==101);
   message_vector v{rtp(102),nullptr};r.incomingAt(v,[](auto){},Receiver::at(0));CHECK(v.size()==1);CHECK(word(*r.report(),16)==102);
  }
  // Callbacks may re-enter without retaining the stats mutex.
  {Receiver r;active(r);message_vector v{sr()};unsigned count=0;r.incomingAt(v,[&](auto){++count;r.requestKeyframe([&](auto p){CHECK(std::to_integer<unsigned>((*p)[1])==206);CHECK(word(*p,4)!=0x12345678);CHECK(word(*p,8)==0x12345678);});},Receiver::at(0));CHECK(count==1);}
  // Bounded deterministic malformed corpus, no sockets or peer creation.
  {Receiver r(90000);uint32_t rng=1234567;for(unsigned n=0;n<10000;++n){rng=rng*1664525+1013904223;auto m=make_message(rng%97,n%2?Message::Control:Message::Binary);for(auto &b:*m){rng=rng*1664525+1013904223;b=byte(rng>>24);}r.feed(m,n);}}
  std::cout<<"PASS RTCP wire assertions="<<checks<<" malformed_cases=10000\n";
 } catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
