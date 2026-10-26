// SPDX-License-Identifier: GPL-3.0-only
using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using HarmonyLib;
using MelonLoader;
using UnityEngine;
[assembly:MelonInfo(typeof(SinmaiAsio.Plugin),"Sinmai ASIO","0.1.2","sinmai-asio contributors")]
[assembly:MelonGame("sega-interactive","Sinmai")]
namespace SinmaiAsio {
    public sealed class Plugin:MelonMod {
        static Settings settings;
        static bool enabled,prepared,installed,started,failed,finalizing,nativeAttempted;
        static uint source;
        static string problem="";
        static string gameRoot,statisticsPath;
        static DateTime nextSnapshot;
        static MethodInfo isInitialized;
        const string GameHash="DC0E46A4133D4EE4781BA6DEB2FE30063DD3F82EE91C31A5A8A7E61E644D9446";
        public override void OnEarlyInitializeMelon() {
            gameRoot=Path.GetDirectoryName(System.Diagnostics.Process.GetCurrentProcess().MainModule.FileName);
            statisticsPath=Path.Combine(gameRoot,"UserData","SinmaiAsio.stats.json");
            try {
                string file=Path.Combine(gameRoot,"SinmaiAsio.toml");
                bool created;
                settings=Configuration.LoadOrCreate(file,Configuration.InstalledDrivers,out created);
                if(created&&!settings.Enabled)MelonLogger.Warning("[SinmaiAsio] 已生成配置：未找到 ASIO 驱动或存在多个候选，请设置 Driver 并启用。 / Configuration created: no ASIO driver or multiple candidates; set Driver and Enabled manually.");
                string experiment=Environment.GetEnvironmentVariable("SINMAI_ASIO_EXPERIMENT");
                source=experiment=="capture"?3u:experiment=="clock"?4u:0u;
                if(!string.IsNullOrEmpty(experiment)&&experiment!="capture"&&experiment!="clock")throw new InvalidOperationException("Unknown SINMAI_ASIO_EXPERIMENT");
                enabled=settings.Enabled||source!=0;
                if(!enabled){MelonLogger.Msg("[SinmaiAsio] Disabled / 已禁用");return;}
                if(settings.BufferSamples!=32&&settings.BufferSamples!=64&&settings.BufferSamples!=128)throw new InvalidOperationException("BufferSamples must be 32, 64, or 128");
                if(settings.LeftChannel<0||settings.RightChannel<0||settings.LeftChannel==settings.RightChannel||string.IsNullOrWhiteSpace(settings.Driver)||settings.Driver.Length>126)throw new InvalidOperationException("Invalid ASIO driver or output channels");
                string managed=Path.Combine(gameRoot,"Sinmai_Data","Managed");
                if(FileHash(Path.Combine(managed,"Assembly-CSharp.dll"))!=GameHash)throw new InvalidOperationException("Unsupported game assembly; only the validated 1.70 profile is allowed");
                var first=Assembly.Load("Assembly-CSharp-firstpass");var game=Assembly.Load("Assembly-CSharp");
                var atom=first.GetType("CriAtomPlugin",true);
                isInitialized=AccessTools.Method(atom,"IsLibraryInitialized");
                Patch(atom,"SetConfigParameters","Configure",null);
                Patch(atom,"InitializeLibrary","BeforeInitialize","AfterInitialize");
                Patch(atom,"FinalizeLibrary","BeforeFinalize","AfterFinalize");
                Patch(game.GetType("Manager.WasapiExclusive",true),"Intialize","SkipWasapi",null);
                // Load by full path. Native binary uses the static MSVC runtime.
                if(Native.LoadLibraryW(Path.Combine(gameRoot,"Sinmai_Data","Plugins","cri_ware_unity.dll"))==IntPtr.Zero)throw new InvalidOperationException("Cannot load CRI library");
                Native.Bind(Path.Combine(gameRoot,"UserLibs","SinmaiAsio","SinmaiAsio.Native.dll"));
                if(Native.SaGetAbiVersion()!=Native.Abi)throw new InvalidOperationException("Native ABI mismatch");
                Diagnostic("Native exports bound; ASIO preparation deferred until game native modules load");
            } catch(Exception ex){Failure(ex);}
        }
        static string FileHash(string path) {using(var sha=SHA256.Create())using(var file=File.OpenRead(path))return BitConverter.ToString(sha.ComputeHash(file)).Replace("-","");}
        void Patch(Type type,string name,string prefix,string postfix) {
            var method=AccessTools.Method(type,name);if(method==null)throw new MissingMethodException(type.FullName,name);
            HarmonyInstance.Patch(method,prefix==null?null:new HarmonyMethod(typeof(Plugin),prefix){priority=HarmonyLib.Priority.First},postfix==null?null:new HarmonyMethod(typeof(Plugin),postfix));
        }
        static bool SkipWasapi(){return !enabled;}
        static void Configure(ref int __9,ref int __10,ref float __12) {
            if(!enabled||failed)return;
            // Verified parameter indices in this pinned game build.
            __9=48000;__10=2;__12=48000f/settings.BufferSamples;
            Diagnostic("CRI configuration: 48000 Hz, 2 channels, serverFrequency="+__12);
        }
        static bool BeforeInitialize() {
            if(!enabled)return true;if(failed)return false;
            try {
                if(!prepared) {
                    // Yamaha ASIO and amdaemon_api both prefer 0x180000000. Loading ASIO early
                    // relocates the game's fixed-base native module and can crash before audio init.
                    var daemon=Native.GetModuleHandleW("amdaemon_api.dll");
                    if(daemon==IntPtr.Zero)throw new InvalidOperationException("amdaemon_api.dll is not loaded yet; refusing early ASIO driver load");
                    if(daemon.ToInt64()!=0x180000000L)throw new InvalidOperationException("amdaemon_api.dll has been relocated; restart without early-loading ASIO modules");
                    var options=new Options {Size=(uint)Marshal.SizeOf(typeof(Options)),Abi=Native.Abi,Frames=(uint)settings.BufferSamples,Left=(uint)settings.LeftChannel,Right=(uint)settings.RightChannel,Driver=settings.Driver,Source=source};
                    nativeAttempted=true;Native.Check(Native.SaInitialize(ref options));prepared=true;
                    var info=new StringBuilder(2048);Native.SaGetDriverInfo(info,2048);
                    Diagnostic("Prepared source="+source+" block="+settings.BufferSamples+" ABI sizes="+Marshal.SizeOf(typeof(Options))+"/"+Marshal.SizeOf(typeof(Stats))+" amdaemonBase=0x"+daemon.ToInt64().ToString("X")+" "+info);
                }
                if(!installed){Native.Check(Native.SaInstallCri());installed=true;Diagnostic("Native output adapter installed");}
                return true;
            }catch(Exception ex){Failure(ex);return false;}
        }
        static void AfterInitialize() {
            if(!enabled||failed||started)return;
            try {Native.Check(Native.SaAfterCriInitialize());Native.Check(Native.SaStart());started=true;nextSnapshot=DateTime.UtcNow;MelonLogger.Msg("[SinmaiAsio] ASIO started: "+settings.Driver+", 48000 Hz, "+settings.BufferSamples+" samples");}
            catch(Exception ex){Failure(ex);}
        }
        static void BeforeFinalize() {
            if(!enabled||!prepared)return;
            // CRI's managed API is ref-counted. Only the final call tears down the native library.
            var type=isInitialized.DeclaringType;var field=AccessTools.Field(type,"initializationCount");
            finalizing=(int)field.GetValue(null)<=1;
            if(finalizing){try{Native.Check(Native.SaStop());started=false;}catch(Exception ex){Failure(ex);}}
        }
        static void AfterFinalize() {
            if(!enabled||!installed||!finalizing)return;
            try{Native.Check(Native.SaRestoreCri());installed=false;}catch(Exception ex){Failure(ex);}
        }
        static void Failure(Exception ex) {
            if(failed)return;
            failed=true;problem="Sinmai ASIO 音频已停止，请退出游戏并检查日志。\nAudio stopped. Exit the game and inspect the log.\n"+ex.GetBaseException().Message;
            MelonLogger.Error("[SinmaiAsio] "+problem+"\n"+ex);
            if(nativeAttempted){try{Native.SaStop();}catch{}}
        }
        public override void OnUpdate() {
            if(!enabled||!prepared||DateTime.UtcNow<nextSnapshot)return;
            nextSnapshot=DateTime.UtcNow.AddSeconds(5);
            try {
                var s=Stats.Create();Native.Check(Native.SaGetStats(ref s));
                if(s.Fault!=0&&!failed)Failure(new InvalidOperationException(FaultReason(s.Fault)));
                if(settings.Diagnostics) {
                    string json="{\"schema\":1,\"source\":"+source+",\"state\":"+s.State+",\"fault\":"+s.Fault+",\"callbacks\":"+s.Callbacks+",\"frames\":"+s.Frames+",\"missingSamplesIncludingWarmup\":"+s.Missing+",\"criCalls\":"+s.CriCalls+",\"criFrames\":"+s.CriFrames+",\"nonzeroFrames\":"+s.Nonzero+",\"queue\":"+s.Queued+",\"peakQueue\":"+s.PeakQueue+",\"criThread\":"+s.CriThread+",\"asioThread\":"+s.AsioThread+",\"threadChanges\":"+s.ThreadChanges+",\"channels\":"+s.Channels+",\"minBlock\":"+s.MinimumBlock+",\"maxBlock\":"+s.MaximumBlock+",\"elapsedSeconds\":"+Number(s.ElapsedSeconds)+",\"reportedOutputMs\":"+Number(s.ReportedOutputMs)+",\"maxCallbackUs\":"+Number(s.MaxCallbackUs)+",\"waitHistogram250us\":["+string.Join(",",s.WaitHistogram)+"],\"criGapHistogram250us\":["+string.Join(",",s.GapHistogram)+"]}";
                    json=json.Substring(0,json.Length-1)+",\"renderCalls\":"+s.RenderCalls+",\"renderFrames\":"+s.RenderFrames+"}";
                    json=json.Substring(0,json.Length-1)+",\"nativeAbi\":"+Native.Abi+",\"missingAfterWarmup\":"+s.MissingAfterWarmup+",\"outputNonzeroFrames\":"+s.OutputNonzero+",\"maximumRenderLockUs\":"+Number(s.MaximumRenderLockUs)+",\"maximumRenderWorkUs\":"+Number(s.MaximumRenderWorkUs)+"}";
                    Directory.CreateDirectory(Path.GetDirectoryName(statisticsPath));File.WriteAllText(statisticsPath,json);
                    MelonLogger.Msg("[SinmaiAsio] source="+source+" state="+s.State+" fault="+s.Fault+" cri="+s.CriFrames+" audible="+s.Nonzero+" callbacks="+s.Callbacks+" missing="+s.Missing+" queue="+s.Queued+" peak="+s.PeakQueue);
                }
            }catch(Exception ex){Failure(ex);}
        }
        static string Number(double x){return x.ToString("R",System.Globalization.CultureInfo.InvariantCulture);}
        static void Diagnostic(string message){if(settings!=null&&settings.Diagnostics)MelonLogger.Msg("[SinmaiAsio] "+message);}
        static string FaultReason(uint code){
            switch(code){
                case 1:return "音频欠载（"+settings.BufferSamples+" samples），请检查缓冲设置后重启。 / ASIO underrun; check buffer settings and restart.";
                case 2:return "声卡驱动请求重置，请重启游戏。 / Driver reset requested; restart the game.";
                case 3:return "采样率发生变化，请恢复 48000 Hz 后重启。 / Sample rate changed; restore 48000 Hz and restart.";
                case 8:return "声卡回调中断，请检查连接后重启。 / Audio callbacks stalled; check the connection and restart.";
                default:return "Native audio fault="+code;
            }
        }
        public override void OnGUI() {if(problem.Length>0)GUI.Box(new Rect(20,20,1000,160),problem);}
        public override void OnApplicationQuit() {if(prepared){try{Native.SaStop();}catch(Exception ex){MelonLogger.Error(ex);}}}
    }
}
