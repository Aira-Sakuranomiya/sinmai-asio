// SPDX-License-Identifier: GPL-3.0-only
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include "api.h"

int wmain(int argc,wchar_t** argv) {
    if(argc!=3){fprintf(stderr,"profile-tests --known|--unknown DLL_PATH\n");return 2;}
    const bool known=wcscmp(argv[1],L"--known")==0;
    if(!known&&wcscmp(argv[1],L"--unknown")!=0)return 2;
    SaOptions options{};options.size=sizeof(options);options.abi=999;options.frames=64;options.source=3;
    wcscpy_s(options.driver,L"unused-capture-test");
    assert(SaInitialize(&options)!=0); // ABI rejection must leave the engine uninitialized.
    SaStats stats;memset(&stats,0xa5,sizeof(stats));stats.size=sizeof(stats);stats.abi=999;
    SaStats before=stats;assert(SaGetStats(&stats)!=0);assert(memcmp(&stats,&before,sizeof(stats))==0);
    options.abi=SA_ABI_VERSION;assert(SaInitialize(&options)==0);
    auto module=LoadLibraryW(argv[2]);if(!module){fprintf(stderr,"Cannot load fixture: %lu\n",GetLastError());return 1;}
    auto* base=reinterpret_cast<unsigned char*>(module);
    unsigned char entry[16],dos[16];memcpy(dos,base,sizeof(dos));void* backend=nullptr;
    if(known){memcpy(entry,base+0x73e50,sizeof(entry));backend=*reinterpret_cast<void**>(base+0x187d80);}
    int result=SaInstallCri();
    char message[2048]{};SaGetError(message,sizeof(message));
    if(known) {
        if(result){fprintf(stderr,"Known profile failed: %s\n",message);return 1;}
        assert(memcmp(entry,base+0x73e50,5)!=0);
        assert(SaRestoreCri()==0);
        assert(memcmp(entry,base+0x73e50,sizeof(entry))==0);
        assert(backend==*reinterpret_cast<void**>(base+0x187d80));
        puts("profile_tests: known module accepted; entry bytes and backend slot fully restored");
    } else {
        assert(result!=0);assert(strstr(message,"SHA256")!=nullptr);
        assert(memcmp(dos,base,sizeof(dos))==0);assert(SaRestoreCri()==0);
        puts("profile_tests: unsupported module rejected before internal-pointer access or hooking");
    }
    assert(SaStop()==0);FreeLibrary(module);return 0;
}
