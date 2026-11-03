// SPDX-License-Identifier: GPL-3.0-only
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include "api.h"

// Execute the actual instruction in an ABI-correct harness and verify restore.
static void check_render_quantum(const unsigned char* instruction,unsigned expected) {
    auto* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));assert(code);
    const unsigned char prefix[]={0x41,0x57};std::memcpy(code,prefix,2);
    std::memcpy(code+2,instruction,6);
    const unsigned char tail[]={0x41,0x8b,0xc7,0x41,0x5f,0xc3};std::memcpy(code+8,tail,6);
    DWORD old=0;assert(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old));assert(FlushInstructionCache(GetCurrentProcess(),code,14));
    auto function=reinterpret_cast<unsigned(*)()>(code);assert(function()==expected);
    assert(VirtualFree(code,0,MEM_RELEASE));
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=3){fprintf(stderr,"profile-tests --known|--unknown DLL_PATH\n");return 2;}
    const bool known=wcscmp(argv[1],L"--known")==0;
    if(!known&&wcscmp(argv[1],L"--unknown")!=0)return 2;
    SaOptions options{};options.size=sizeof(options);options.abi=999;options.frames=64;options.source=3;
    wcscpy_s(options.driver,L"unused-capture-test");
    assert(SaInitialize(&options)!=0); // ABI rejection must leave the engine uninitialized.
    SaStats stats;memset(&stats,0xa5,sizeof(stats));stats.size=sizeof(stats);stats.abi=999;
    SaStats before=stats;assert(SaGetStats(&stats)!=0);assert(memcmp(&stats,&before,sizeof(stats))==0);
    options.abi=SA_ABI_VERSION;options.frames=0;assert(SaInitialize(&options)==0);
    assert(options.frames==128); // Capture-only mode has no driver and reports its fixed probe default.
    SaStats initial{};initial.size=sizeof(initial);initial.abi=SA_ABI_VERSION;assert(SaGetStats(&initial)==0);
    assert(initial.callbackGapCount==0&&initial.callbackMaxGapUs==0&&initial.validPositionCallbacks==0&&initial.resyncRequests==0&&initial.latencyChangeRequests==0);
    auto module=LoadLibraryW(argv[2]);if(!module){fprintf(stderr,"Cannot load fixture: %lu\n",GetLastError());return 1;}
    auto* base=reinterpret_cast<unsigned char*>(module);
    unsigned char entry[16],dos[16],quantum[6];memcpy(dos,base,sizeof(dos));void* backend=nullptr;
    if(known){memcpy(quantum,base+0x7cce5,sizeof(quantum));check_render_quantum(quantum,256);memcpy(entry,base+0x73e50,sizeof(entry));backend=*reinterpret_cast<void**>(base+0x187d80);}
    int result=SaInstallCri();
    char message[2048]{};SaGetError(message,sizeof(message));
    if(known) {
        if(result){fprintf(stderr,"Known profile failed: %s\n",message);return 1;}
        assert(memcmp(entry,base+0x73e50,5)!=0);
        assert(memcmp(quantum,base+0x7cce5,sizeof(quantum))!=0);check_render_quantum(base+0x7cce5,32);
        assert(SaRestoreCri()==0);
        assert(memcmp(entry,base+0x73e50,sizeof(entry))==0);
        assert(memcmp(quantum,base+0x7cce5,sizeof(quantum))==0);check_render_quantum(base+0x7cce5,256);
        assert(backend==*reinterpret_cast<void**>(base+0x187d80));
        puts("profile_tests: known module accepted; entry bytes and backend slot fully restored");
    } else {
        assert(result!=0);assert(strstr(message,"SHA256")!=nullptr);
        assert(memcmp(dos,base,sizeof(dos))==0);assert(SaRestoreCri()==0);
        puts("profile_tests: unsupported module rejected before internal-pointer access or hooking");
    }
    assert(SaStop()==0);FreeLibrary(module);return 0;
}
