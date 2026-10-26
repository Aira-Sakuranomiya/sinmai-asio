// SPDX-License-Identifier: GPL-3.0-only
#include "audio.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <thread>
int main() {
    unsigned char p[8]{};
    sa::encode(-1,17,p);assert(p[0]==0&&p[1]==0&&p[2]==0x80);
    sa::encode(1,17,p);assert(p[0]==0xff&&p[1]==0xff&&p[2]==0x7f);
    sa::encode(2,16,p);assert(p[0]==0xff&&p[1]==0x7f);
    sa::encode(-2,18,p);int32_t n;memcpy(&n,p,4);assert(n==INT32_MIN);
    sa::encode(std::numeric_limits<float>::quiet_NaN(),18,p);memcpy(&n,p,4);assert(n==0);
    sa::encode(.25f,19,p);float f;memcpy(&f,p,4);assert(f==.25f);
    sa::encode(-.5f,20,p);double d;memcpy(&d,p,8);assert(d==-.5);
    assert(sa::sample_bytes(27)==0&&sa::sample_bytes(999)==0);
    sa::Ring ring;ring.reset(32);float l[128],r[128];for(unsigned i=0;i<128;++i){l[i]=float(i);r[i]=-float(i);}
    assert(ring.push(l,r,128,123)==32);assert(ring.push(l,r,1,0)==0);
    sa::Frame frame;for(unsigned i=0;i<17;++i){assert(ring.pop(frame));assert(frame.l==i&&frame.r==-float(i)&&frame.timestamp==123);}
    assert(ring.push(l,r,32,456)==17);
    for(unsigned i=17;i<32;++i){assert(ring.pop(frame));assert(frame.l==i&&frame.timestamp==123);}
    for(unsigned i=0;i<17;++i){assert(ring.pop(frame));assert(frame.l==i&&frame.timestamp==456);}
    assert(!ring.pop(frame));
    ring.reset(32);assert(ring.push(l,r,17,789)==17);sa::Frame block[32];
    assert(ring.take(block,32)==17);assert(ring.size()==0);
    for(unsigned i=0;i<17;++i)assert(block[i].l==float(i)&&block[i].r==-float(i)&&block[i].timestamp==789);
    // Exercise acquire/release publication and stereo order under concurrent wraparound.
    ring.reset(64);constexpr unsigned total=1000000;
    std::thread producer([&]{for(unsigned i=0;i<total;){float a=float(i),b=-a;if(ring.push(&a,&b,1,i))++i;else std::this_thread::yield();}});
    for(unsigned i=0;i<total;){if(ring.pop(frame)){assert(frame.l==float(i)&&frame.r==-float(i)&&frame.timestamp==i);++i;}else std::this_thread::yield();}
    producer.join();assert(!ring.pop(frame));puts("audio_tests: all checks passed");
}
