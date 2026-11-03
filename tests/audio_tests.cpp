// SPDX-License-Identifier: GPL-3.0-only
#include "audio.h"
#include "timing.h"
#include "capture.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <thread>
int main() {
    // Capture is a side-channel of the final encoded ASIO bytes, not the playback queue.
    for(int type:{16,17,18,19,20}) {
        unsigned char encoded[8]{};sa::encode(-.5f,type,encoded);
        assert(sa::decode_sample(encoded,type)==-.5f);
        sa::encode(.25f,type,encoded);assert(sa::decode_sample(encoded,type)==.25f);
    }
    sa::OutputCapture capture;capture.configure(8,19,19);
    float capLeft[12],capRight[12];for(unsigned i=0;i<12;++i){capLeft[i]=float(i);capRight[i]=-float(i);}
    capture.append(capLeft,capRight,6);capture.append(capLeft+6,capRight+6,6);
    capture.request_snapshot();assert(!capture.snapshot());
    capture.append(capLeft,capRight,2);
    const auto* frozen=capture.snapshot();assert(frozen&&frozen->frames==8);
    FILE* wave=nullptr;assert(tmpfile_s(&wave)==0&&wave);
    sa::write_capture_wave(wave,*frozen);assert(std::ftell(wave)==56+8*8);
    std::rewind(wave);unsigned char waveHeader[56];assert(std::fread(waveHeader,1,56,wave)==56);
    assert(!std::memcmp(waveHeader,"RIFF",4)&&!std::memcmp(waveHeader+48,"data",4));
    assert(waveHeader[20]==3&&waveHeader[22]==2&&waveHeader[34]==32&&waveHeader[52]==64);
    float recorded[16];assert(std::fread(recorded,sizeof(float),16,wave)==16);std::fclose(wave);
    for(unsigned i=0;i<8;++i)assert(recorded[2*i]==float(i+4)&&recorded[2*i+1]==-float(i+4));
    const auto frozenCopy=frozen->channel[0];
    for(unsigned i=0;i<100;++i)capture.append(capLeft,capRight,12);
    assert(frozen->channel[0]==frozenCopy); // Saving cannot race with writes to the other bank.
    capture.finish_snapshot();capture.request_snapshot();capture.append(capLeft,capRight,1);
    frozen=capture.snapshot();assert(frozen&&frozen->frames==8);capture.finish_snapshot();
    sa::OutputCapture disabled;bool captureRejected=false;
    try{disabled.request_snapshot();}catch(...){captureRejected=true;}assert(captureRejected);
    captureRejected=false;try{disabled.configure(4,16,24);}catch(...){captureRejected=true;}assert(captureRejected);
    auto gap=sa::observe_gap(1000,1000000,48);
    assert(gap.microseconds==1000&&gap.excessMicroseconds==0&&!gap.over150Percent&&!gap.over200Percent);
    gap=sa::observe_gap(1500,1000000,48);assert(!gap.over150Percent);
    gap=sa::observe_gap(1501,1000000,48);assert(gap.over150Percent&&!gap.over200Percent&&gap.excessMicroseconds==501);
    gap=sa::observe_gap(2000,1000000,48);assert(!gap.over200Percent);
    gap=sa::observe_gap(2001,1000000,48);assert(gap.over200Percent);
    assert(sa::observe_gap(0,1000000,256).microseconds==0);
    assert(sa::observe_gap(-1,1000000,256).microseconds==0);
    assert(sa::observe_gap(1000,0,256).microseconds==0);
    assert(sa::observe_gap(1000,1000000,0).microseconds==0);
    sa::PositionTracker position;
    assert(!position.observe(true,0,256).discontinuity);
    assert(!position.observe(true,256,256).discontinuity);
    auto step=position.observe(true,768,256);
    assert(step.discontinuity&&step.forwardGapSamples==256&&!step.backward);
    step=position.observe(true,768,256);assert(step.discontinuity&&step.repeated);
    step=position.observe(true,800,256);assert(step.discontinuity&&step.shortStep);
    step=position.observe(true,0,256);assert(step.discontinuity&&step.backward);
    assert(!position.observe(false,0,256).discontinuity);
    assert(!position.observe(true,65536,256).discontinuity);
    assert(!position.observe(true,65792,256).discontinuity);
    // Correct across a 32-bit low-word rollover and for non-power-of-two sizes.
    sa::PositionTracker widePosition;
    const uint64_t base=(uint64_t(1)<<32)-96;
    assert(!widePosition.observe(true,base,192).discontinuity);
    assert(!widePosition.observe(true,base+192,192).discontinuity);
    // Auto mode uses exactly the driver preference; manual mode remains explicit.
    constexpr unsigned sizes[]={32,48,64,96,128,192,256,384,512,768,1024,1536,2048};
    for(unsigned size:sizes) {
        assert(sa::resolve_buffer(0,long(size))==size);
        assert(sa::resolve_buffer(size,256)==size);
        sa::Ring large;large.reset(size);
        std::array<float,sa::MaxBufferSamples> left{},right{};
        std::array<sa::Frame,sa::MaxBufferSamples> output{};
        for(unsigned i=0;i<size;++i){left[i]=float(i);right[i]=-float(i);}
        assert(large.push(left.data(),right.data(),size,42)==size);
        assert(large.take(output.data(),size-1)==size-1);
        assert(large.push(left.data(),right.data(),size,43)==size-1);
        assert(large.take(output.data(),size)==size);
        assert(output[0].l==float(size-1)&&output[0].timestamp==42);
        for(unsigned i=1;i<size;++i)assert(output[i].l==float(i-1)&&output[i].r==-float(i-1)&&output[i].timestamp==43);
        assert(large.size()==0);
    }
    assert(sa::resolve_buffer(0,0)==0&&sa::resolve_buffer(0,-1)==0);
    assert(sa::resolve_buffer(0,4096)==0&&sa::resolve_buffer(0,100)==0);
    assert(sa::resolve_buffer(100,128)==0);
    // The soft knee preserves quiet PCM, stereo ratios and zero latency (no history).
    float softLeft=.5f,softRight=-.25f;
    auto peak=sa::soft_limit(softLeft,softRight);
    assert(softLeft==.5f&&softRight==-.25f&&!peak.limited&&!peak.overRange&&peak.peak==.5);
    softLeft=2;softRight=-1;peak=sa::soft_limit(softLeft,softRight);
    assert(peak.overRange&&peak.limited&&peak.peak==2&&peak.invalidSamples==0);
    assert(softLeft<=.980001f&&softLeft>.9f&&softRight==-.5f*softLeft);
    softLeft=-2;softRight=1;sa::soft_limit(softLeft,softRight);
    assert(softLeft>=-.980001f&&softRight==-.5f*softLeft);
    softLeft=std::numeric_limits<float>::quiet_NaN();softRight=std::numeric_limits<float>::infinity();
    peak=sa::soft_limit(softLeft,softRight);
    assert(softLeft==0&&softRight==0&&peak.invalidSamples==2&&peak.peak==0&&!peak.limited);
    softLeft=.25f;softRight=-std::numeric_limits<float>::infinity();
    peak=sa::soft_limit(softLeft,softRight);assert(softLeft==.25f&&softRight==0&&peak.invalidSamples==1);
    softLeft=std::numeric_limits<float>::max();softRight=-softLeft;
    peak=sa::soft_limit(softLeft,softRight);
    assert(peak.overRange&&std::isfinite(softLeft)&&std::fabs(softLeft)<=.980001f&&softRight==-softLeft);
    float previous=0;
    for(unsigned i=0;i<=40000;++i) {
        softLeft=float(i)/10000;softRight=-softLeft;
        sa::soft_limit(softLeft,softRight);
        assert(softLeft>=previous&&softLeft<=.980001f&&softRight==-softLeft);previous=softLeft;
    }
    softLeft=.9f;softRight=0;sa::soft_limit(softLeft,softRight);float atKnee=softLeft;
    softLeft=.90001f;sa::soft_limit(softLeft,softRight);
    assert(softLeft>=atKnee&&softLeft-atKnee<.000011f);
    // A full-scale transient cannot affect the next quiet sample or leave a release tail.
    softLeft=10;softRight=10;sa::soft_limit(softLeft,softRight);
    softLeft=.1f;softRight=-.2f;sa::soft_limit(softLeft,softRight);
    assert(softLeft==.1f&&softRight==-.2f);
    softLeft=2;softRight=-1;peak=sa::soft_limit(softLeft,softRight,false);
    assert(softLeft==2&&softRight==-1&&peak.overRange&&!peak.limited&&peak.peak==2);
    softLeft=std::numeric_limits<float>::quiet_NaN();softRight=.25f;
    peak=sa::soft_limit(softLeft,softRight,false);
    assert(softLeft==0&&softRight==.25f&&peak.invalidSamples==1&&!peak.limited);
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
