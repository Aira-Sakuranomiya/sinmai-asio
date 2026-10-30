// SPDX-License-Identifier: GPL-3.0-only
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define SA_EXPORTS
#include <windows.h>
#include <objbase.h>
#include <bcrypt.h>
#include <avrt.h>
#include <cstdio>
#include <string>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <vector>
#include "iasiodrv.h"
#include "api.h"
#include "audio.h"
#include "MinHook.h"
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"advapi32.lib")
#pragma comment(lib,"bcrypt.lib")
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"avrt.lib")

namespace {
constexpr uint32_t ABI=SA_ABI_VERSION;
constexpr char CriHash[]="1F6555296765D4B7D8AB1BCD5C8D44F515764933B6149D42330A44A872C402C3";
struct Engine {
    SaOptions options{};
    sa::Ring ring;
    std::array<sa::Frame,sa::MaxBufferSamples> callbackBlock{};
    std::mutex messageMutex;
    std::string error,driverInfo;
    std::atomic<unsigned> state{0},fault{0},inflight{0};
    std::atomic_flag producerActive=ATOMIC_FLAG_INIT;
    std::atomic<uint64_t> callbacks{0},frames{0},missing{0},criCalls{0},criFrames{0},nonzero{0},threadChanges{0};
    std::atomic<uint64_t> renderCalls{0},renderFrames{0};
    std::atomic<uint64_t> missingAfterWarmup{0},outputNonzero{0};
    std::atomic<unsigned> peakQueue{0},criThread{0},asioThread{0},channels{0},minBlock{UINT_MAX},maxBlock{0};
    std::atomic<int64_t> maxCallback{0},lastCri{0},lastRender{0},lastAsio{0},startTick{0};
    std::atomic<int64_t> maximumRenderLock{0},maximumRenderWork{0};
    std::array<std::atomic<uint64_t>,64> wait{},gap{};
    std::atomic<bool> accepting{false},quit{false};
    HANDLE worker=nullptr,command=nullptr,done=nullptr;
    std::atomic<int> operation{0},result{0};
    DWORD ownerThread=0;
    IASIO* driver=nullptr;
    ASIOBufferInfo buffers[2]{};
    ASIOChannelInfo info[2]{};
    ASIOCallbacks handlers{};
    bool comOwned=false,buffersCreated=false,started=false,outputReady=false;
    long latencyIn=0,latencyOut=0;
    double sampleRate=0;
    int64_t frequency=0;
    unsigned tonePhase=0;
    float tone[48000]{};
    unsigned char* cri=nullptr;
    void* originalBackend=nullptr;
    void* customBackend[38]{};
    bool installed=false;
    void (*setBackend)(void*)=nullptr;
    bool hookInitialized=false,hookCreated=false;
    bool renderHookCreated=false;
    void (*renderOriginal)(void*)=nullptr;
    std::atomic<void*> renderRack{nullptr};
    std::atomic<void*> scheduledRack{nullptr};
    std::atomic<bool> renderStop{false};
    HANDLE renderThread=nullptr,renderEvent=nullptr,renderReady=nullptr;
} e;
static_assert(sizeof(e.customBackend)==0x130,"The complete CRI backend interface must be retained");
int64_t ticks() noexcept {LARGE_INTEGER x;QueryPerformanceCounter(&x);return x.QuadPart;}
template<class T> void maximum(std::atomic<T>& a,T x) noexcept {auto v=a.load();while(v<x&&!a.compare_exchange_weak(v,x)) {}}
template<class T> void minimum(std::atomic<T>& a,T x) noexcept {auto v=a.load();while(v>x&&!a.compare_exchange_weak(v,x)) {}}
void fail(unsigned code) noexcept { unsigned zero=0;e.fault.compare_exchange_strong(zero,code);e.accepting=false; }
unsigned bucket(int64_t t) noexcept {return unsigned(std::min<int64_t>(63,std::max<int64_t>(0,t)*4000/e.frequency));}
void check(ASIOError r,const char* what) {if(r!=ASE_OK && r!=ASE_SUCCESS)throw std::runtime_error(std::string(what)+" ASIO="+std::to_string(r));}
void set_error(const std::string& s) {std::lock_guard<std::mutex> guard(e.messageMutex);e.error=s;}
int error(const char* s) noexcept {try{set_error(s);}catch(...){}return -1;}

void process(long index,ASIOBool) noexcept {
    e.inflight.fetch_add(1,std::memory_order_acq_rel);
    if(index<0||index>1) {fail(5);e.inflight.fetch_sub(1);return;}
    const auto begin=ticks();e.lastAsio=begin;e.asioThread=GetCurrentThreadId();
    const bool active=e.accepting.load(std::memory_order_acquire);
    unsigned missing=0,audibleOutput=0;
    auto* block=e.callbackBlock.data();unsigned available=0;
    if(active&&(e.options.source==0||e.options.source==4)) {
        available=e.ring.take(block,e.options.frames);missing=e.options.frames-available;
        // Publish room for the complete next block once, then request rendering. Never wait here.
        if(e.renderEvent&&!e.fault)SetEvent(e.renderEvent);
    }
    for(unsigned i=0;i<e.options.frames;++i) {
        sa::Frame frame{};
        if(active) {
            if(e.options.source==0||e.options.source==4) {
                if(i<available)frame=block[i];
                if(i<available&&frame.timestamp)++e.wait[bucket(begin-frame.timestamp)];
            } else if(e.options.source==2) {
                frame.l=frame.r=e.tone[e.tonePhase];e.tonePhase=(e.tonePhase+440)%48000;
            }
        }
        if(e.options.source==4)frame.l=frame.r=0;
        if(std::fabs(frame.l)>1e-7f||std::fabs(frame.r)>1e-7f)++audibleOutput;
        for(unsigned ch=0;ch<2;++ch) {
            auto* out=static_cast<unsigned char*>(e.buffers[ch].buffers[index]);
            sa::encode(ch?frame.r:frame.l,e.info[ch].type,out+i*sa::sample_bytes(e.info[ch].type));
        }
    }
    if(active) {
        ++e.callbacks;e.frames+=e.options.frames;e.outputNonzero+=audibleOutput;
        // Missing frames are silence for this callback only. Keep the driver and render worker
        // running so PCM resumes on the next callback with available data; never latch a fault.
        // Preserve the one-second warmup distinction for diagnostics without per-underrun logs.
        if(missing) {
            e.missing+=missing;
            if(begin-e.startTick>e.frequency)e.missingAfterWarmup+=missing;
        }
    }
    if(e.outputReady)e.driver->outputReady();
    maximum(e.maxCallback,ticks()-begin);
    e.inflight.fetch_sub(1,std::memory_order_release);
}
ASIOTime* process_time(ASIOTime* time,long index,ASIOBool direct) noexcept {process(index,direct);return time;}
void rate_changed(ASIOSampleRate rate) noexcept {if(rate!=48000.0)fail(3);}
long asio_message(long selector,long value,void*,double*) noexcept {
    if(selector==kAsioSelectorSupported) return value==kAsioEngineVersion||value==kAsioResetRequest||value==kAsioBufferSizeChange||value==kAsioResyncRequest||value==kAsioLatenciesChanged||value==kAsioSupportsTimeInfo;
    if(selector==kAsioEngineVersion)return 2;
    if(selector==kAsioSupportsTimeInfo)return 1;
    if(selector==kAsioResetRequest||selector==kAsioBufferSizeChange||selector==kAsioResyncRequest||selector==kAsioLatenciesChanged){fail(2);return 1;}
    return 0;
}
bool buffer_allowed(long n,long low,long high,long granularity) {
    if(n<low||n>high)return false;
    if(granularity==-1)return n>0&&(n&(n-1))==0;
    if(granularity==0)return n==low&&low==high;
    return granularity>0&&(n-low)%granularity==0;
}
void initialize_driver() {
    HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(hr))throw std::runtime_error("CoInitializeEx failed");e.comOwned=true;
    std::wstring key=L"SOFTWARE\\ASIO\\"+std::wstring(e.options.driver);
    wchar_t id[128]{};DWORD size=sizeof(id);
    if(RegGetValueW(HKEY_LOCAL_MACHINE,key.c_str(),L"CLSID",RRF_RT_REG_SZ,nullptr,id,&size)!=ERROR_SUCCESS)throw std::runtime_error("ASIO driver is not registered (64-bit registry)");
    CLSID clsid{};if(FAILED(CLSIDFromString(id,&clsid)))throw std::runtime_error("Invalid ASIO driver CLSID");
    hr=CoCreateInstance(clsid,nullptr,CLSCTX_INPROC_SERVER,clsid,reinterpret_cast<void**>(&e.driver));
    if(FAILED(hr))throw std::runtime_error("Cannot create ASIO driver: HRESULT="+std::to_string(uint32_t(hr)));
    if(!e.driver->init(GetDesktopWindow())) {char text[124]{};e.driver->getErrorMessage(text);throw std::runtime_error(std::string("ASIO init: ")+text);}
    long inputs=0,outputs=0,low=0,high=0,preferred=0,granularity=0;
    check(e.driver->getChannels(&inputs,&outputs),"getChannels");
    check(e.driver->getBufferSize(&low,&high,&preferred,&granularity),"getBufferSize");
    std::ostringstream report;
    report<<"driver outputs="<<outputs<<" inputs="<<inputs<<" buffer min="<<low<<" max="<<high<<" preferred="<<preferred<<" granularity="<<granularity;
    {std::lock_guard<std::mutex> guard(e.messageMutex);e.driverInfo=report.str();}
    if(e.options.left>=unsigned(outputs)||e.options.right>=unsigned(outputs)||e.options.left==e.options.right)throw std::runtime_error("Output channels are unavailable or identical");
    if(!buffer_allowed(e.options.frames,low,high,granularity))throw std::runtime_error("Requested buffer is not supported: "+report.str());
    check(e.driver->canSampleRate(48000),"canSampleRate");
    check(e.driver->getSampleRate(&e.sampleRate),"getSampleRate");
    if(e.sampleRate!=48000) {check(e.driver->setSampleRate(48000),"setSampleRate");check(e.driver->getSampleRate(&e.sampleRate),"getSampleRate");}
    if(e.sampleRate!=48000)throw std::runtime_error("Driver did not accept 48000 Hz");
    for(unsigned ch=0;ch<2;++ch) {
        e.info[ch].channel=long(ch?e.options.right:e.options.left);e.info[ch].isInput=ASIOFalse;
        check(e.driver->getChannelInfo(&e.info[ch]),"getChannelInfo");
        if(!sa::sample_bytes(e.info[ch].type))throw std::runtime_error("Unsupported ASIO sample type: "+std::to_string(e.info[ch].type));
        e.buffers[ch].channelNum=e.info[ch].channel;e.buffers[ch].isInput=ASIOFalse;
    }
    e.handlers={process,rate_changed,asio_message,process_time};
    check(e.driver->createBuffers(e.buffers,2,long(e.options.frames),&e.handlers),"createBuffers");e.buffersCreated=true;
    for(unsigned ch=0;ch<2;++ch)for(unsigned half=0;half<2;++half) {
        if(!e.buffers[ch].buffers[half])throw std::runtime_error("Driver returned a null buffer");
        std::memset(e.buffers[ch].buffers[half],0,e.options.frames*sa::sample_bytes(e.info[ch].type));
    }
    check(e.driver->getLatencies(&e.latencyIn,&e.latencyOut),"getLatencies");
    e.outputReady=e.driver->outputReady()==ASE_OK;
    report<<" sampleRate="<<e.sampleRate<<" types="<<e.info[0].type<<","<<e.info[1].type<<" outputLatencySamples="<<e.latencyOut;
    {std::lock_guard<std::mutex> guard(e.messageMutex);e.driverInfo=report.str();}
}
void stop_driver() {
    e.accepting=false;
    if(e.started) {check(e.driver->stop(),"stop");e.started=false;}
    const auto deadline=ticks()+2*e.frequency;
    while(e.inflight.load(std::memory_order_acquire)&&ticks()<deadline)Sleep(1);
    if(e.inflight)throw std::runtime_error("ASIO callbacks did not stop; buffers retained for safety");
}
DWORD WINAPI worker(void*) {
    try {initialize_driver();e.result=0;e.state=1;}
    catch(const std::exception& ex){set_error(ex.what());e.result=-1;e.state=4;}
    catch(...){set_error("Unexpected ASIO initialization exception");e.result=-1;e.state=4;}
    SetEvent(e.done);
    bool finished=false;
    while(!finished) {
        const auto signaled=WaitForSingleObject(e.command,10);
        const auto op=signaled==WAIT_OBJECT_0?e.operation.exchange(0):0;
        try {
            if(e.started && ticks()-e.lastAsio.load()>e.frequency)fail(8);
            if(e.fault && e.started){stop_driver();e.state=4;set_error("ASIO stopped: fault="+std::to_string(e.fault.load())+" (2=reset,3=rate change,4=CRI format,5=driver callback,6=CRI thread)");}
            if(op==1) {
                if(e.state!=1)throw std::runtime_error("Driver is not prepared");
                e.startTick=ticks();e.lastAsio=e.startTick.load();e.accepting=true;check(e.driver->start(),"start");e.started=true;e.state=2;e.result=0;
            } else if(op==2) {stop_driver();e.state=e.fault?4:3;e.result=0;}
            else if(op==3) {stop_driver();finished=true;e.result=0;}
        } catch(const std::exception& ex) {e.accepting=false;set_error(ex.what());e.result=-1;e.state=4;}
          catch(...) {e.accepting=false;set_error("Unexpected ASIO control exception");e.result=-1;e.state=4;}
        if(op)SetEvent(e.done);
    }
    // Teardown is on the same COM apartment as creation. Never free buffers during a callback.
    if(!e.inflight && !e.started){if(e.buffersCreated)e.driver->disposeBuffers();if(e.driver)e.driver->Release();if(e.comOwned)CoUninitialize();}
    return 0;
}
int command(int op) {
    if(!e.worker)return 0;
    ResetEvent(e.done);e.operation=op;SetEvent(e.command);
    if(WaitForSingleObject(e.done,10000)!=WAIT_OBJECT_0)return error("ASIO control timed out; restart required");
    return e.result.load();
}
std::string file_hash(const wchar_t* path) {
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot read CRI module");
    BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    unsigned char digest[32],buffer[32768];DWORD read=0;bool success=false;
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0&&BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)>=0) {
        success=true;
        while(true){if(!ReadFile(file,buffer,sizeof(buffer),&read,nullptr)){success=false;break;}if(!read)break;if(BCryptHashData(hash,buffer,read,0)<0){success=false;break;}}
        if(success)success=BCryptFinishHash(hash,digest,sizeof(digest),0)>=0;
    }
    if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);CloseHandle(file);
    if(!success)throw std::runtime_error("CRI SHA256 failed");
    char text[65]{};for(unsigned i=0;i<32;++i)sprintf_s(text+2*i,3,"%02X",digest[i]);return text;
}
void profile() {
    HMODULE module=GetModuleHandleW(L"cri_ware_unity.dll");if(!module)throw std::runtime_error("CRI library is not loaded");
    wchar_t path[32768];if(!GetModuleFileNameW(module,path,32768)||file_hash(path)!=CriHash)throw std::runtime_error("Unsupported CRI DLL SHA256; no internal pointers changed");
    e.cri=reinterpret_cast<unsigned char*>(module);
    // Guard executable entry bytes as well as disk identity before installing reversible detours.
    const unsigned char signature[]={0x48,0x89,0x0d,0x29,0x3f,0x11,0x00,0xc3};
    if(std::memcmp(e.cri+0x73e50,signature,sizeof(signature)))throw std::runtime_error("CRI backend setter signature mismatch");
    auto** pseudo=reinterpret_cast<void**>(e.cri+0x126e50);
    if(pseudo[8]!=e.cri+0x745e4||pseudo[14]!=e.cri+0x74734)throw std::runtime_error("CRI pseudo backend signature mismatch");
    const unsigned char renderSignature[]={0x48,0x8b,0xc4,0x48,0x89,0x58,0x10,0x48,0x89,0x68,0x18,0x48,0x89,0x70,0x20};
    if(std::memcmp(e.cri+0x7cca0,renderSignature,sizeof(renderSignature)))throw std::runtime_error("CRI render entry signature mismatch");
}
void scheduled_render(void* rack) noexcept {
    // The normal server still handles commands/decoding, but only the ASIO-demand worker may
    // run the main rack's rendering routine. Keeping both clocks active would duplicate audio.
    void* expected=nullptr;
    if(!e.scheduledRack.compare_exchange_strong(expected,rack)&&expected!=rack)fail(9);
}
DWORD WINAPI render_worker(void*) {
    DWORD task=0;HANDLE mmcss=AvSetMmThreadCharacteristicsW(L"Pro Audio",&task);
    if(mmcss)AvSetMmThreadPriority(mmcss,AVRT_PRIORITY_CRITICAL);
    else SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_HIGHEST);
    while(true) {
        const auto result=WaitForSingleObject(e.renderEvent,50);
        if(e.renderStop.load())break;
        if(result!=WAIT_OBJECT_0)continue;
        if(e.fault)continue;
        auto* rack=e.renderRack.load();
        if(rack&&e.ring.size()<e.options.frames) {
            const auto before=ticks();
            reinterpret_cast<void(*)()>(e.cri+0x16a88)();
            const auto locked=ticks();
            e.renderOriginal(rack);
            reinterpret_cast<void(*)()>(e.cri+0x16b24)();
            maximum(e.maximumRenderLock,locked-before);maximum(e.maximumRenderWork,ticks()-locked);
            SetEvent(e.renderReady);
        }
    }
    if(mmcss)AvRevertMmThreadCharacteristics(mmcss);return 0;
}
void start_render_worker(void* rack) {
    e.renderRack=rack;e.renderStop=false;
    e.renderEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);e.renderReady=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!e.renderEvent||!e.renderReady)throw std::runtime_error("Cannot create render events");
    e.renderThread=CreateThread(nullptr,0,render_worker,nullptr,0,nullptr);
    if(!e.renderThread)throw std::runtime_error("Cannot create native render worker");
    SetEvent(e.renderEvent);
    if(WaitForSingleObject(e.renderReady,2000)!=WAIT_OBJECT_0||e.ring.size()!=e.options.frames)throw std::runtime_error("ASIO-demand rendering did not prime one complete block");
}
void stop_render_worker() {
    if(e.renderThread){e.renderStop=true;SetEvent(e.renderEvent);if(WaitForSingleObject(e.renderThread,2000)!=WAIT_OBJECT_0)throw std::runtime_error("CRI render worker did not stop; native module must remain loaded");CloseHandle(e.renderThread);e.renderThread=nullptr;}
    if(e.renderEvent){CloseHandle(e.renderEvent);e.renderEvent=nullptr;}
    if(e.renderReady){CloseHandle(e.renderReady);e.renderReady=nullptr;}
    e.renderRack=nullptr;
}
// All renderer functions below run on the CRI server thread, never on the ASIO callback.
int backend_begin(void*) noexcept {return 1;}
void output_data(void*,int channels,int frames,float**) noexcept {
    if(channels!=2||frames<=0||frames>4096){fail(4);return;}
    const auto now=ticks();const auto last=e.lastRender.exchange(now);
    if(last)++e.gap[bucket(now-last)];
    ++e.renderCalls;e.renderFrames+=unsigned(frames);
}
void* backend_submit(void* context,unsigned char* packet) noexcept {
    if(!packet)return nullptr;
    const unsigned count=*reinterpret_cast<uint32_t*>(packet+0x24)/sizeof(float);
    // Packet +0x11 is allocated channel capacity (8), not active output channel count.
    // Pseudo's set-channels method stores the actual count at context +0.
    const unsigned channels=context?*static_cast<uint32_t*>(context):0;
    e.channels=channels;
    if(channels!=2) {fail(40);return packet;}
    if(packet[0x11]<channels||packet[0x11]>8||count>4096) {fail(41);return packet;}
    auto** data=reinterpret_cast<float**>(packet+0x28);
    if(!data[0]||!data[1]) {fail(42);return packet;}
    const auto now=ticks();const auto thread=GetCurrentThreadId();
    // CRI may execute serialized server work on different OS threads. Reject overlap, not migration.
    if(e.producerActive.test_and_set(std::memory_order_acquire)){fail(6);return packet;}
    struct ProducerScope {~ProducerScope(){e.producerActive.clear(std::memory_order_release);}} scope;
    auto previous=e.criThread.exchange(thread);if(previous && previous!=thread)++e.threadChanges;
    unsigned n=count;
    if(e.options.source!=3) {
        // At most one selected ASIO block is queued. Backpressure preserves CRI's unconsumed packet.
        auto rendered=e.lastRender.load();
        n=e.ring.push(data[0],data[1],count,rendered?rendered:now);maximum(e.peakQueue,e.ring.size());
    }
    if(n) {
        ++e.criCalls;e.criFrames+=n;e.channels=channels;minimum(e.minBlock,n);maximum(e.maxBlock,n);
        uint64_t audible=0;for(unsigned i=0;i<n;++i)if(std::fabs(data[0][i])>1e-7f||std::fabs(data[1][i])>1e-7f)++audible;e.nonzero+=audible;
        // Verified CRI packet helper: consumes bytes per planar channel and advances channel pointers.
        reinterpret_cast<void(*)(void*,uint32_t)>(e.cri+0x86b74)(packet,n*sizeof(float));
        *reinterpret_cast<uint64_t*>(static_cast<unsigned char*>(context)+0x38)+=n;
    }
    if(*reinterpret_cast<uint32_t*>(packet+0x24))return packet;
    // The backend owns fully consumed packets. Return them to their originating CRI queue;
    // returning null without recycling would starve the renderer after its first packet.
    reinterpret_cast<void(*)(void*,int,void*)>(e.cri+0x86cf0)(*reinterpret_cast<void**>(packet+8),packet[0x10],packet);
    return nullptr;
}
int backend_active(void*) noexcept {return 1;}
int backend_latency(void*) noexcept {return int(e.ring.size())+std::max(0L,e.latencyOut);}
void register_backend(void* backend) noexcept {
    if(backend==e.cri+0x117bb0)e.setBackend(e.customBackend);
    else {fail(7);e.setBackend(backend);}
}
template<class F> int guarded(F f) noexcept {try{f();return 0;}catch(const std::exception& ex){return error(ex.what());}catch(...){return error("Unexpected native exception");}}
int copy_string(const std::string& text,char* buffer,unsigned size) {if(!buffer||!size)return -1;strncpy_s(buffer,size,text.c_str(),_TRUNCATE);return text.size()<size?0:1;}
}

SA_API int SaGetAbiVersion() noexcept {return ABI;}
SA_API int SaInitialize(const SaOptions* options) noexcept {
    return guarded([&]{
        if(!options||options->size!=sizeof(SaOptions)||options->abi!=ABI)throw std::runtime_error("ABI/options mismatch");
        if(e.state!=0)throw std::runtime_error("Already initialized; restart required");
        if(options->frames!=32&&options->frames!=48&&options->frames!=64&&options->frames!=96&&options->frames!=128&&options->frames!=192&&options->frames!=256&&options->frames!=384&&options->frames!=512&&options->frames!=768&&options->frames!=1024&&options->frames!=1536&&options->frames!=2048)throw std::runtime_error("Buffer must be 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536 or 2048 samples");
        if(options->source>4||!options->driver[0]||options->driver[127])throw std::runtime_error("Invalid driver/source");
        e.options=*options;e.ownerThread=GetCurrentThreadId();e.ring.reset(options->frames);
        LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);e.frequency=frequency.QuadPart;
        for(unsigned i=0;i<48000;++i)e.tone[i]=float(0.01*std::sin(6.283185307179586*i/48000));
        if(options->source==3){e.state=1;return;}
        e.command=CreateEventW(nullptr,FALSE,FALSE,nullptr);e.done=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!e.command||!e.done)throw std::runtime_error("Cannot create control events");
        e.worker=CreateThread(nullptr,0,worker,nullptr,0,nullptr);
        if(!e.worker)throw std::runtime_error("Cannot create ASIO control thread");
        if(WaitForSingleObject(e.done,15000)!=WAIT_OBJECT_0)throw std::runtime_error("ASIO driver initialization timed out");
        if(e.result)throw std::runtime_error(e.error);
    });
}
SA_API int SaStart() noexcept {
    if(e.fault){try{set_error("Native pre-start fault="+std::to_string(e.fault.load())+" channels="+std::to_string(e.channels.load()));}catch(...){}return -1;}
    if(e.options.source==0||e.options.source==4) {if(!e.installed)return error("CRI backend has not been installed");}
    if(e.options.source==3){e.startTick=ticks();e.state=2;return 0;}
    return guarded([&]{if(command(1))throw std::runtime_error(e.error);});
}
SA_API int SaStop() noexcept {
    return guarded([&]{
        if(e.options.source==3){e.state=3;return;}
        if(e.worker){if(command(3))throw std::runtime_error(e.error);if(WaitForSingleObject(e.worker,5000)!=WAIT_OBJECT_0)throw std::runtime_error("ASIO worker did not exit");CloseHandle(e.worker);e.worker=nullptr;CloseHandle(e.command);CloseHandle(e.done);e.command=e.done=nullptr;e.state=e.fault?4:3;}
        stop_render_worker();
    });
}
SA_API int SaGetError(char* buffer,uint32_t size) noexcept {return guarded([&]{std::lock_guard<std::mutex> lock(e.messageMutex);if(copy_string(e.error,buffer,size)<0)throw std::runtime_error("Invalid error buffer");});}
SA_API int SaGetDriverInfo(char* buffer,uint32_t size) noexcept {return guarded([&]{std::lock_guard<std::mutex> lock(e.messageMutex);if(copy_string(e.driverInfo,buffer,size)<0)throw std::runtime_error("Invalid info buffer");});}
SA_API int SaGetStats(SaStats* s) noexcept {
    if(!s||s->size!=sizeof(SaStats)||s->abi!=ABI)return -1;
    s->state=e.state;s->fault=e.fault;s->callbacks=e.callbacks;s->frames=e.frames;s->missing=e.missing;s->criCalls=e.criCalls;s->criFrames=e.criFrames;s->nonzero=e.nonzero;s->threadChanges=e.threadChanges;
    s->renderCalls=e.renderCalls;s->renderFrames=e.renderFrames;
    s->missingAfterWarmup=e.missingAfterWarmup;s->outputNonzero=e.outputNonzero;
    s->queued=e.ring.size();s->peakQueue=e.peakQueue;s->criThread=e.criThread;s->asioThread=e.asioThread;s->channels=e.channels;s->minimumBlock=e.minBlock==UINT_MAX?0:e.minBlock.load();s->maximumBlock=e.maxBlock;
    s->sampleRate=e.sampleRate;s->reportedInputMs=e.latencyIn/48.0;s->reportedOutputMs=e.latencyOut/48.0;s->maxCallbackUs=e.frequency?e.maxCallback.load()*1000000.0/e.frequency:0;const auto start=e.startTick.load();s->elapsedSeconds=start?(ticks()-start)/double(e.frequency):0;
    s->maximumRenderLockUs=e.frequency?e.maximumRenderLock.load()*1000000.0/e.frequency:0;s->maximumRenderWorkUs=e.frequency?e.maximumRenderWork.load()*1000000.0/e.frequency:0;
    for(unsigned i=0;i<64;++i){s->waitHistogram[i]=e.wait[i];s->gapHistogram[i]=e.gap[i];}return 0;
}
SA_API int SaInstallCri() noexcept {
    return guarded([&]{
        if(e.state!=1||e.installed)throw std::runtime_error("Install requires prepared state");profile();
        if(*reinterpret_cast<uint32_t*>(e.cri+0x1602d8))throw std::runtime_error("CRI already initialized; install must precede initialization");
        auto** global=reinterpret_cast<void**>(e.cri+0x187d80);
        e.originalBackend=*global;
        if(e.originalBackend && e.originalBackend!=e.cri+0x117bb0)throw std::runtime_error("Another native backend is installed; refusing to replace it");
        std::memcpy(e.customBackend,e.cri+0x126e50,sizeof(e.customBackend));
        e.customBackend[12]=reinterpret_cast<void*>(backend_begin);
        // Slot 13 initializes silent buffers synchronously; retain CRI's original implementation.
        // Only slot 14 is the regular render submission path.
        e.customBackend[14]=reinterpret_cast<void*>(backend_submit);
        e.customBackend[16]=reinterpret_cast<void*>(backend_active);
        e.customBackend[35]=reinterpret_cast<void*>(backend_latency);
        // CRI re-registers WASAPI DURING initialization. Intercept the verified setter, not just its data slot.
        auto status=MH_Initialize();if(status!=MH_OK)throw std::runtime_error("MinHook initialization failed");e.hookInitialized=true;
        status=MH_CreateHook(e.cri+0x73e50,reinterpret_cast<void*>(register_backend),reinterpret_cast<void**>(&e.setBackend));
        if(status!=MH_OK){MH_Uninitialize();e.hookInitialized=false;throw std::runtime_error("Cannot prepare backend registration hook");}e.hookCreated=true;
        if(e.options.source==0||e.options.source==4) {
            status=MH_CreateHook(e.cri+0x7cca0,reinterpret_cast<void*>(scheduled_render),reinterpret_cast<void**>(&e.renderOriginal));
            if(status!=MH_OK){MH_Uninitialize();e.hookCreated=e.hookInitialized=false;throw std::runtime_error("Cannot prepare ASIO-demand render hook");}e.renderHookCreated=true;
        }
        status=MH_EnableHook(MH_ALL_HOOKS);
        if(status!=MH_OK){MH_Uninitialize();e.hookCreated=e.hookInitialized=e.renderHookCreated=false;throw std::runtime_error("Cannot activate CRI hooks");}
        e.installed=true;
    });
}
SA_API int SaAfterCriInitialize() noexcept {
    return guarded([&]{
        if(!e.installed)throw std::runtime_error("Backend not installed");
        auto** racks=*reinterpret_cast<void***>(e.cri+0x187f48);
        if(!racks||!racks[0])throw std::runtime_error("CRI ASR rack 0 was not initialized");
        auto* rack=static_cast<unsigned char*>(racks[0]);auto** output=*reinterpret_cast<void***>(rack+8);
        if(!output||output[0]!=e.customBackend)throw std::runtime_error("CRI did not select our backend; no ASIO playback started");
        auto* context=static_cast<unsigned char*>(output[1]);
        if(!context||*reinterpret_cast<uint32_t*>(context)!=2)throw std::runtime_error("CRI active channels="+std::to_string(context?*reinterpret_cast<uint32_t*>(context):0)+" instead of requested 2; native serverFrequency="+std::to_string(*reinterpret_cast<float*>(e.cri+0x160310)));
        // Bound a render iteration to one requested block instead of CRI's 105%-then-align budget.
        reinterpret_cast<void(*)()>(e.cri+0x16a88)();
        *reinterpret_cast<uint32_t*>(rack+0x80)=e.options.frames;
        reinterpret_cast<void(*)()>(e.cri+0x16b24)();
        if(*reinterpret_cast<void**>(rack+0xa8))throw std::runtime_error("Existing CRI output callback detected; refusing conflict");
        reinterpret_cast<void(*)(void*,void*)>(e.cri+0x2eca0)(reinterpret_cast<void*>(output_data),nullptr);
        if(e.options.source==0||e.options.source==4)start_render_worker(rack);
    });
}
SA_API int SaRestoreCri() noexcept {
    return guarded([&]{
        if(!e.installed)return;
        // Restoration is only safe AFTER CRI finalization; live rack objects still reference our table.
        if(*reinterpret_cast<uint32_t*>(e.cri+0x1602d8))throw std::runtime_error("Refusing to restore backend while CRI is initialized");
        auto** global=reinterpret_cast<void**>(e.cri+0x187d80);
        void* current=*global;
        if(current==e.customBackend)InterlockedCompareExchangePointer(global,e.originalBackend,e.customBackend);
        else if(current && current!=e.originalBackend)throw std::runtime_error("CRI backend was replaced by another module");
        if(e.hookCreated){if(MH_DisableHook(e.cri+0x73e50)!=MH_OK||MH_RemoveHook(e.cri+0x73e50)!=MH_OK)throw std::runtime_error("Cannot remove CRI registration hook");e.hookCreated=false;}
        if(e.renderHookCreated){if(MH_DisableHook(e.cri+0x7cca0)!=MH_OK||MH_RemoveHook(e.cri+0x7cca0)!=MH_OK)throw std::runtime_error("Cannot remove CRI rendering hook");e.renderHookCreated=false;}
        if(e.hookInitialized){MH_Uninitialize();e.hookInitialized=false;}
        e.installed=false;
    });
}
