// SPDX-License-Identifier: MPL-2.0
#include <rtc/rtcpnackresponder.hpp>
#include <rtc/message.hpp>
#include <cassert>
#include <iostream>

using namespace rtc;
static message_ptr packet(unsigned seq) {
 auto m=std::make_shared<Message>(1212);
 (*m)[0]=byte(0x80); (*m)[1]=byte(96);
 (*m)[2]=byte(seq>>8);(*m)[3]=byte(seq&255);
 return m;
}
static unsigned request(RtcpNackResponder &r,unsigned seq) {
 auto m=std::make_shared<Message>(16,Message::Control);
 (*m)[0]=byte(0x81);(*m)[1]=byte(205);(*m)[3]=byte(3);
 (*m)[12]=byte(seq>>8);(*m)[13]=byte(seq&255);
 message_vector in{m};unsigned count=0;
 r.incoming(in,[&](message_ptr p){assert(p->size()==1212);++count;});return count;
}
static void check(unsigned n,unsigned first,unsigned last,unsigned expectedFirst) {
 RtcpNackResponder r;
 message_vector batch;
 for(unsigned i=0;i<n;++i)batch.push_back(packet(i));
 unsigned wire=0;r.outgoing(batch,[&](message_ptr){++wire;});
 auto old=request(r,first),recent=request(r,last);
 assert(wire==0&&old==expectedFirst&&recent==1);
 std::cout<<"{\"packets_preloaded\":"<<n<<",\"wire_callbacks\":"<<wire
 <<",\"first_seq\":"<<first<<",\"first_retransmittable\":"<<old
 <<",\"last_seq\":"<<last<<",\"last_retransmittable\":"<<recent<<"}\n";
}
int main(){
 static_assert(RtcpNackResponder::DefaultMaxSize==512);
 check(512,0,511,1);check(513,0,512,0);check(876,0,875,0);
 // Three 171-packet batches consume 513 entries, without needing any packet loss.
 RtcpNackResponder r;
 for(unsigned b=0;b<3;++b){message_vector v;for(unsigned i=0;i<171;++i)v.push_back(packet(b*171+i));r.outgoing(v,[](message_ptr){});}
 assert(request(r,0)==0&&request(r,1)==1&&request(r,512)==1);
 std::cout<<"{\"three_batches_171\":\"oldest_evicted\"}\n";
}
