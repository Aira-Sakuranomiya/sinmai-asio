// SPDX-License-Identifier: GPL-3.0-only
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "api.h"
int main(int argc,char** argv) {
    SaOptions o{};o.size=sizeof(o);o.abi=SA_ABI_VERSION;o.frames=128;o.left=0;o.right=1;o.source=1;
    wcscpy_s(o.driver,L"Yamaha Steinberg USB ASIO");
    unsigned seconds=0;
    for(int i=1;i<argc;++i) {
        if(!strcmp(argv[i],"--buffer")&&i+1<argc)o.frames=unsigned(atoi(argv[++i]));
        else if(!strcmp(argv[i],"--driver")&&i+1<argc){if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,argv[++i],-1,o.driver,128)){fprintf(stderr,"Invalid driver name\n");return 2;}}
        else if(!strcmp(argv[i],"--left")&&i+1<argc)o.left=unsigned(atoi(argv[++i]));
        else if(!strcmp(argv[i],"--right")&&i+1<argc)o.right=unsigned(atoi(argv[++i]));
        else if(!strcmp(argv[i],"--silence")&&i+1<argc)seconds=unsigned(atoi(argv[++i]));
        else if(!strcmp(argv[i],"--tone")&&i+1<argc){seconds=unsigned(atoi(argv[++i]));o.source=2;}
        else {fprintf(stderr,"Usage: asio-probe [--driver name] [--buffer 32|48|64|96|128|192|256|384|512|768|1024|1536|2048] [--left index] [--right index] [--silence seconds | --tone seconds]\n");return 2;}
    }
    if(seconds>1800){fprintf(stderr,"Duration must be <=1800 seconds\n");return 2;}
    char text[2048]{};int status=SaInitialize(&o);
    SaGetDriverInfo(text,sizeof(text));printf("%s\n",text);
    if(status){SaGetError(text,sizeof(text));fprintf(stderr,"Prepare failed: %s\n",text);SaStop();return 1;}
    if(seconds) {
        if(SaStart()){SaGetError(text,sizeof(text));fprintf(stderr,"Start failed: %s\n",text);SaStop();return 1;}
        for(unsigned i=0;i<seconds*10;++i) {Sleep(100);SaStats s{};s.size=sizeof(s);s.abi=SA_ABI_VERSION;SaGetStats(&s);if(s.fault){status=1;break;}}
    }
    SaStats s{};s.size=sizeof(s);s.abi=SA_ABI_VERSION;SaGetStats(&s);
    printf("abi=%u sizeofOptions=%zu sizeofStats=%zu callbacks=%llu frames=%llu missing=%llu fault=%u elapsed=%.3f maxCallbackUs=%.3f reportedOutputMs=%.3f\n",s.abi,sizeof(o),sizeof(s),s.callbacks,s.frames,s.missing,s.fault,s.elapsedSeconds,s.maxCallbackUs,s.reportedOutputMs);
    if(seconds&&!s.callbacks){fprintf(stderr,"Driver produced no callbacks\n");status=1;}
    if(SaStop())status=1;
    SaGetError(text,sizeof(text));if(text[0])fprintf(stderr,"%s\n",text);
    return status;
}
