// SPDX-License-Identifier: GPL-3.0-only
using System;
using System.Runtime.InteropServices;
using System.Text;
namespace SinmaiAsio {
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode,Pack=8)]
    internal struct Options {
        public uint Size,Abi,Frames,Left,Right,Source;
        [MarshalAs(UnmanagedType.ByValTStr,SizeConst=128)] public string Driver;
    }
    [StructLayout(LayoutKind.Sequential,Pack=8)]
    internal struct Stats {
        public uint Size,Abi,State,Fault;
        public ulong Callbacks,Frames,Missing,CriCalls,CriFrames,Nonzero,ThreadChanges;
        public ulong RenderCalls,RenderFrames;
        public ulong MissingAfterWarmup,OutputNonzero;
        public uint Queued,PeakQueue,CriThread,AsioThread,Channels,MinimumBlock,MaximumBlock,Reserved;
        public double SampleRate,ReportedInputMs,ReportedOutputMs,MaxCallbackUs,ElapsedSeconds;
        public double MaximumRenderLockUs,MaximumRenderWorkUs;
        [MarshalAs(UnmanagedType.ByValArray,SizeConst=64)] public ulong[] WaitHistogram;
        [MarshalAs(UnmanagedType.ByValArray,SizeConst=64)] public ulong[] GapHistogram;
        public static Stats Create() {return new Stats {Size=(uint)Marshal.SizeOf(typeof(Stats)),Abi=Native.Abi,WaitHistogram=new ulong[64],GapHistogram=new ulong[64]};}
    }
    internal static class Native {
        internal const uint Abi=2;
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int Simple();
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int Initialize(ref Options options);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int GetStats(ref Stats stats);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl,CharSet=CharSet.Ansi)] internal delegate int Text(StringBuilder text,uint size);
        internal static Simple SaGetAbiVersion,SaInstallCri,SaAfterCriInitialize,SaStart,SaStop,SaRestoreCri;
        internal static Initialize SaInitialize;
        internal static GetStats SaGetStats;
        internal static Text SaGetError,SaGetDriverInfo;
        [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] internal static extern IntPtr LoadLibraryW(string path);
        [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] internal static extern IntPtr GetModuleHandleW(string name);
        [DllImport("kernel32.dll",CharSet=CharSet.Ansi,ExactSpelling=true,SetLastError=true)] static extern IntPtr GetProcAddress(IntPtr module,string name);
        static T Export<T>(IntPtr module,string name) where T:class {
            var address=GetProcAddress(module,name);if(address==IntPtr.Zero)throw new EntryPointNotFoundException(name);
            return (T)(object)Marshal.GetDelegateForFunctionPointer(address,typeof(T));
        }
        internal static void Bind(string path) {
            var module=LoadLibraryW(path);if(module==IntPtr.Zero)throw new InvalidOperationException("Cannot load native module: Win32="+Marshal.GetLastWin32Error());
            // Unity Mono's P/Invoke search does not reliably find DLLs in nested UserLibs directories.
            // Bind exact exports from the explicitly loaded module; keep delegates for process lifetime.
            SaGetAbiVersion=Export<Simple>(module,"SaGetAbiVersion");SaInitialize=Export<Initialize>(module,"SaInitialize");
            SaInstallCri=Export<Simple>(module,"SaInstallCri");SaAfterCriInitialize=Export<Simple>(module,"SaAfterCriInitialize");
            SaStart=Export<Simple>(module,"SaStart");SaStop=Export<Simple>(module,"SaStop");SaRestoreCri=Export<Simple>(module,"SaRestoreCri");
            SaGetStats=Export<GetStats>(module,"SaGetStats");SaGetError=Export<Text>(module,"SaGetError");SaGetDriverInfo=Export<Text>(module,"SaGetDriverInfo");
        }
        internal static void Check(int result) {if(result==0)return;var text=new StringBuilder(2048);SaGetError(text,2048);throw new InvalidOperationException(text.ToString());}
    }
}
