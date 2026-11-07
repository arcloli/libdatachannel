// SPDX-License-Identifier: MPL-2.0
#include <rtc/rtc.hpp>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <mutex>
using namespace rtc;
using namespace std::chrono_literals;
int main() {
 InitLogger(LogLevel::Warning);
 Configuration cfg;cfg.bindAddress="127.0.0.1";cfg.iceServers.clear();
 std::atomic<unsigned> media{0},data{0},reports{0};
 std::atomic<bool> failed{false};std::string error;std::mutex errorMutex;
 auto fail=[&](const std::exception &e){std::lock_guard<std::mutex> l(errorMutex);error=e.what();failed=true;};
 {
 PeerConnection a(cfg),b(cfg);
 a.onLocalDescription([&](Description d){try{b.setRemoteDescription(d);}catch(const std::exception&e){fail(e);}});
 b.onLocalDescription([&](Description d){try{a.setRemoteDescription(d);}catch(const std::exception&e){fail(e);}});
 a.onLocalCandidate([&](Candidate c){try{b.addRemoteCandidate(c);}catch(const std::exception&e){fail(e);}});
 b.onLocalCandidate([&](Candidate c){try{a.addRemoteCandidate(c);}catch(const std::exception&e){fail(e);}});
 std::shared_ptr<DataChannel> rxData;std::shared_ptr<Track> rxTrack;
 b.onDataChannel([&](auto dc){dc->onMessage([&](message_variant msg){if(auto p=std::get_if<string>(&msg);p&&*p=="offline-probe")++data;});std::atomic_store(&rxData,dc);});
 b.onTrack([&](auto track){track->setMediaHandler(std::make_shared<RtcpReceivingSession>());track->onMessage([&](message_variant msg){if(auto p=std::get_if<binary>(&msg);p&&p->size()==13)++media;});std::atomic_store(&rxTrack,track);});
 Description::Video vd("video",Description::Direction::SendOnly);vd.addH264Codec(96);vd.addSSRC(0x12345678,"offline");auto tx=a.addTrack(vd);auto dc=a.createDataChannel("offline-only");
 tx->onMessage([&](message_variant msg){if(auto p=std::get_if<binary>(&msg);p&&p->size()==32&&std::to_integer<unsigned>((*p)[1])==201&&std::to_integer<unsigned>((*p)[19])==102)++reports;});
 const auto deadline=std::chrono::steady_clock::now()+15s;
 while(!failed&&(!dc->isOpen()||!tx->isOpen()||!std::atomic_load(&rxTrack)||!std::atomic_load(&rxData))&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(10ms);
 if(!dc->isOpen()||!tx->isOpen()||failed){std::cerr<<"FAIL loopback connect "<<error<<'\n';return 1;}
 dc->send("offline-probe");
 for(unsigned seq=100;seq<103;++seq){binary p(13);p[0]=byte(0x80);p[1]=byte(96);p[2]=byte(seq>>8);p[3]=byte(seq);p[8]=byte(0x12);p[9]=byte(0x34);p[10]=byte(0x56);p[11]=byte(0x78);p[12]=byte(0x65);tx->send(p);}
 while((data<1||media<3)&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(10ms);
 if(data!=1||media!=3){std::cerr<<"FAIL received data="<<data<<" media="<<media<<'\n';return 1;}
 binary sr(28);sr[0]=byte(0x80);sr[1]=byte(200);sr[3]=byte(6);sr[4]=byte(0x12);sr[5]=byte(0x34);sr[6]=byte(0x56);sr[7]=byte(0x78);sr[8]=byte(1);tx->send(sr);
 while(reports<1&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(10ms);
 if(reports!=1){std::cerr<<"FAIL SRTCP report="<<reports<<'\n';return 1;}
 tx->close();dc->close();a.close();b.close();
 }
 auto done=Cleanup();if(done.wait_for(5s)!=std::future_status::ready){std::cerr<<"FAIL cleanup timeout\n";return 1;}
 std::cout<<"PASS loopback DTLS/SRTP media=3 SRTCP RR=1 SCTP data=1 cleanup_ready=1 (synthetic packets, no decoder)\n";
}
