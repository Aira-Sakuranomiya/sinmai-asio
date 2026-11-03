// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "audio.h"
#include <atomic>
#include <cstdio>
#include <stdexcept>
#include <vector>
namespace sa {
inline float decode_sample(const unsigned char* p,int type) noexcept {
    if(type==16){int16_t n;std::memcpy(&n,p,2);return float(double(n)/32768.0);}
    if(type==17){int32_t n=int32_t(p[0])|(int32_t(p[1])<<8)|(int32_t(p[2])<<16);if(n&0x800000)n-=0x1000000;return float(double(n)/8388608.0);}
    if(type==18){int32_t n;std::memcpy(&n,p,4);return float(double(n)/2147483648.0);}
    if(type==19){float n;std::memcpy(&n,p,4);return n;}
    if(type==20){double n;std::memcpy(&n,p,8);return float(n);}
    return 0;
}
struct CaptureBank {
    std::vector<unsigned char> channel[2];
    unsigned capacity=0,position=0,frames=0;
    int type[2]{};
    unsigned bytes[2]{};
    void configure(unsigned count,int left,int right) {
        capacity=count;type[0]=left;type[1]=right;position=frames=0;
        for(unsigned ch=0;ch<2;++ch){bytes[ch]=sample_bytes(type[ch]);channel[ch].assign(size_t(count)*bytes[ch],0);}
    }
    void append(const void* left,const void* right,unsigned count) noexcept {
        const void* input[2]={left,right};
        const unsigned retained=std::min(count,capacity),skip=count-retained;
        const unsigned first=std::min(retained,capacity-position);
        for(unsigned ch=0;ch<2;++ch) {
            const auto* source=static_cast<const unsigned char*>(input[ch])+size_t(skip)*bytes[ch];
            std::memcpy(channel[ch].data()+size_t(position)*bytes[ch],source,size_t(first)*bytes[ch]);
            if(retained>first)std::memcpy(channel[ch].data(),source+size_t(first)*bytes[ch],size_t(retained-first)*bytes[ch]);
        }
        position=(position+retained)%capacity;frames=std::min(capacity,frames+retained);
    }
};
class OutputCapture {
    CaptureBank banks_[2];
    unsigned active_=0,frozen_=0;
    bool enabled_=false;
    // 0=idle, 1=snapshot requested, 2=old bank frozen and published.
    std::atomic<unsigned> request_{0};
public:
    void configure(unsigned frames,int left,int right) {
        if(!frames||frames>48000*30||!sample_bytes(left)||!sample_bytes(right))throw std::runtime_error("Invalid output capture size or format");
        enabled_=false;
        banks_[0].configure(frames,left,right);banks_[1].configure(frames,left,right);
        active_=frozen_=0;request_=0;enabled_=true;
    }
    bool enabled() const noexcept{return enabled_;}
    void append(const void* left,const void* right,unsigned frames) noexcept {
        if(!enabled_)return;
        if(request_.load(std::memory_order_acquire)==1) {
            frozen_=active_;active_^=1;
            banks_[active_].frames=banks_[active_].position=0;
            request_.store(2,std::memory_order_release);
        }
        banks_[active_].append(left,right,frames);
    }
    void request_snapshot() {
        if(!enabled_)throw std::runtime_error("Output capture is not enabled");
        unsigned expected=0;request_.compare_exchange_strong(expected,1,std::memory_order_release);
    }
    const CaptureBank* snapshot() const noexcept {
        return request_.load(std::memory_order_acquire)==2?&banks_[frozen_]:nullptr;
    }
    void finish_snapshot() noexcept {request_.store(0,std::memory_order_release);}
};
inline void wave_u16(unsigned char* p,unsigned n) noexcept {p[0]=static_cast<unsigned char>(n);p[1]=static_cast<unsigned char>(n>>8);}
inline void wave_u32(unsigned char* p,uint32_t n) noexcept {for(unsigned i=0;i<4;++i)p[i]=static_cast<unsigned char>(n>>(8*i));}
inline void write_capture_wave(FILE* file,const CaptureBank& bank) {
    if(!bank.frames)throw std::runtime_error("No audio is available in the capture buffer yet");
    unsigned char header[56]{};const uint32_t bytes=bank.frames*8;
    std::memcpy(header,"RIFF",4);wave_u32(header+4,48+bytes);std::memcpy(header+8,"WAVEfmt ",8);
    wave_u32(header+16,16);wave_u16(header+20,3);wave_u16(header+22,2);wave_u32(header+24,48000);
    wave_u32(header+28,48000*8);wave_u16(header+32,8);wave_u16(header+34,32);
    std::memcpy(header+36,"fact",4);wave_u32(header+40,4);wave_u32(header+44,bank.frames);
    std::memcpy(header+48,"data",4);wave_u32(header+52,bytes);
    if(std::fwrite(header,1,sizeof(header),file)!=sizeof(header))throw std::runtime_error("Capture header write failed");
    const unsigned oldest=(bank.position+bank.capacity-bank.frames)%bank.capacity;
    float block[1024]{};
    for(unsigned start=0;start<bank.frames;) {
        const unsigned count=std::min(512u,bank.frames-start);
        for(unsigned i=0;i<count;++i)for(unsigned ch=0;ch<2;++ch) {
            const unsigned index=(oldest+start+i)%bank.capacity;
            block[2*i+ch]=decode_sample(bank.channel[ch].data()+size_t(index)*bank.bytes[ch],bank.type[ch]);
        }
        if(std::fwrite(block,sizeof(float)*2,count,file)!=count)throw std::runtime_error("Capture audio write failed");
        start+=count;
    }
}
}