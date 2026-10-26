// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stdint.h>
constexpr uint32_t SA_ABI_VERSION=2;
#ifdef SA_EXPORTS
#define SA_API extern "C" __declspec(dllexport)
#else
#define SA_API extern "C" __declspec(dllimport)
#endif
// Pack=8 on both sides. Do not change layout without incrementing ABI.
struct SaOptions { uint32_t size,abi,frames,left,right,source; wchar_t driver[128]; };
// source: 0=CRI playback, 1=standalone silence, 2=standalone quiet tone,
// 3=CRI capture-only probe, 4=CRI clock probe (ASIO consumes, output muted).
struct SaStats {
    uint32_t size,abi,state,fault;
    uint64_t callbacks,frames,missing,criCalls,criFrames,nonzero,threadChanges;
    uint64_t renderCalls,renderFrames;
    uint64_t missingAfterWarmup,outputNonzero;
    uint32_t queued,peakQueue,criThread,asioThread,channels,minimumBlock,maximumBlock,reserved;
    double sampleRate,reportedInputMs,reportedOutputMs,maxCallbackUs,elapsedSeconds;
    double maximumRenderLockUs,maximumRenderWorkUs;
    uint64_t waitHistogram[64],gapHistogram[64];
};
static_assert(sizeof(void*)==8,"Only Windows x64 is supported");
static_assert(sizeof(SaOptions)==280,"Options layout changed; update the ABI and managed binding");
static_assert(sizeof(SaStats)==1216,"Stats layout changed; update the ABI and managed binding");
SA_API int SaGetAbiVersion() noexcept;
SA_API int SaInitialize(const SaOptions* options) noexcept;
SA_API int SaStart() noexcept;
SA_API int SaStop() noexcept;
SA_API int SaGetStats(SaStats* stats) noexcept;
SA_API int SaGetError(char* buffer,uint32_t size) noexcept;
SA_API int SaGetDriverInfo(char* buffer,uint32_t size) noexcept;
SA_API int SaInstallCri() noexcept;
SA_API int SaAfterCriInitialize() noexcept;
SA_API int SaRestoreCri() noexcept;
