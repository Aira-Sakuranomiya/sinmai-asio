// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <cstdint>
namespace sa {
struct CallbackGap {
    double microseconds=0,excessMicroseconds=0;
    bool over150Percent=false,over200Percent=false;
};
inline CallbackGap observe_gap(int64_t delta,int64_t frequency,unsigned frames) noexcept {
    CallbackGap result;
    if(delta<=0||frequency<=0||!frames)return result;
    result.microseconds=double(delta)*1000000.0/double(frequency);
    const double period=double(frames)*1000000.0/48000.0;
    result.excessMicroseconds=std::max(0.0,result.microseconds-period);
    result.over150Percent=result.microseconds>period*1.5;
    result.over200Percent=result.microseconds>period*2.0;
    return result;
}
struct PositionStep {
    bool discontinuity=false,backward=false,repeated=false,shortStep=false;
    uint64_t forwardGapSamples=0;
};
// Callback-local history. Missing positions break the comparison chain rather than
// comparing non-adjacent callbacks and falsely reporting skipped samples.
class PositionTracker {
    bool valid_=false;
    uint64_t last_=0;
public:
    PositionStep observe(bool valid,uint64_t position,unsigned frames) noexcept {
        PositionStep result;
        if(valid&&valid_) {
            if(position<last_)result.discontinuity=result.backward=true;
            else {
                const auto delta=position-last_;
                if(delta!=frames) {
                    result.discontinuity=true;
                    if(!delta)result.repeated=true;
                    else if(delta<frames)result.shortStep=true;
                    else result.forwardGapSamples=delta-frames;
                }
            }
        }
        valid_=valid;last_=position;
        return result;
    }
};
}