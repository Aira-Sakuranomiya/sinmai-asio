// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace sa {
constexpr unsigned MaxBufferSamples=2048;
inline unsigned sample_bytes(int type) noexcept {
    switch(type) { case 16:return 2;case 17:return 3;case 18:case 19:return 4;case 20:return 8;default:return 0; }
}
// ASIO little-endian, full-width formats only. Aligned/DSD/unknown formats fail closed.
inline void encode(float value, int type, void* destination) noexcept {
    const double x = std::isfinite(value) ? std::clamp(double(value), -1.0, 1.0) : 0.0;
    auto* p=static_cast<unsigned char*>(destination);
    if(type==19) { float f=static_cast<float>(x); std::memcpy(p,&f,4);return; }
    if(type==20) { std::memcpy(p,&x,8);return; }
    const unsigned bytes=sample_bytes(type), bits=bytes*8;
    if(!bytes) return;
    const int64_t scale=int64_t(1)<<(bits-1);
    const int64_t n=x<=-1 ? -scale : x>=1 ? scale-1 : static_cast<int64_t>(x*double(scale));
    const uint64_t u=static_cast<uint64_t>(n);
    for(unsigned i=0;i<bytes;++i) p[i]=static_cast<unsigned char>(u>>(i*8));
}
struct Frame { float l=0, r=0; int64_t timestamp=0; };
// One producer (CRI server), one consumer (ASIO); capacity is the selected block size.
class Ring {
    std::array<Frame,MaxBufferSamples> data_{};
    alignas(64) std::atomic<uint64_t> read_{0};
    alignas(64) std::atomic<uint64_t> write_{0};
    unsigned capacity_=64;
public:
    void reset(unsigned n) noexcept {capacity_=n;read_=0;write_=0;}
    unsigned size() const noexcept {const auto r=read_.load(std::memory_order_acquire);return std::min(capacity_,unsigned(write_.load(std::memory_order_acquire)-r));}
    unsigned push(const float* l,const float* r,unsigned count,int64_t time) noexcept {
        const auto w=write_.load(std::memory_order_relaxed), rd=read_.load(std::memory_order_acquire);
        const unsigned n=std::min(count,capacity_-unsigned(w-rd));
        for(unsigned i=0;i<n;++i) data_[(w+i)%capacity_]={l[i],r[i],time};
        write_.store(w+n,std::memory_order_release);return n;
    }
    bool pop(Frame& f) noexcept {
        const auto r=read_.load(std::memory_order_relaxed),w=write_.load(std::memory_order_acquire);
        if(r==w)return false;f=data_[r%capacity_];read_.store(r+1,std::memory_order_release);return true;
    }
    unsigned take(Frame* destination,unsigned count) noexcept {
        const auto r=read_.load(std::memory_order_relaxed),w=write_.load(std::memory_order_acquire);
        const unsigned n=std::min(count,unsigned(w-r));
        for(unsigned i=0;i<n;++i)destination[i]=data_[(r+i)%capacity_];
        read_.store(r+n,std::memory_order_release);return n;
    }
};
}
