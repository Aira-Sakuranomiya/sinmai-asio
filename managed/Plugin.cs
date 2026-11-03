// SPDX-License-Identifier: GPL-3.0-only
using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using HarmonyLib;
using MelonLoader;
using UnityEngine;
[assembly:MelonInfo(typeof(SinmaiAsio.Plugin),"Sinmai ASIO","0.2.0","Aira-Sakuranomiya")]
[assembly:MelonGame("sega-interactive","Sinmai")]
namespace SinmaiAsio {
    public sealed class Plugin:MelonMod {
        static Settings settings;
        static bool enabled,prepared,installed,started,failed,finalizing,nativeAttempted;
        static uint source;
        static string problem="";
        static string gameRoot,statisticsPath;
        static DateTime nextSnapshot;
        static MethodInfo isInitialized,setConfig;
        static object[] configArguments;
        static int bufferSamples;
        static float serverFrequencyOverride;
        static bool captureEnabled;
        static int captureBusy;
        static string captureNotice,latestStatistics;
        public override void OnEarlyInitializeMelon() {
            // MelonLoader writes UTF-8; keep the console decoder and managed writer aligned.
            try {Console.OutputEncoding=new UTF8Encoding(false);Native.SetConsoleOutputCP(65001);}
            catch(Exception ex){MelonLogger.Warning("[SinmaiAsio] Cannot configure UTF-8 console output: "+ex.Message);}
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
                serverFrequencyOverride=Configuration.ParseServerFrequencyOverride(Environment.GetEnvironmentVariable("SINMAI_ASIO_SERVER_FREQUENCY"));
                if(serverFrequencyOverride>0)MelonLogger.Warning("[SinmaiAsio] 诊断处理频率覆盖 / Diagnostic server frequency override: "+serverFrequencyOverride+" Hz");
                if(settings.BufferSamples!=0&&settings.BufferSamples!=32&&settings.BufferSamples!=48&&settings.BufferSamples!=64&&settings.BufferSamples!=96&&settings.BufferSamples!=128&&settings.BufferSamples!=192&&settings.BufferSamples!=256&&settings.BufferSamples!=384&&settings.BufferSamples!=512&&settings.BufferSamples!=768&&settings.BufferSamples!=1024&&settings.BufferSamples!=1536&&settings.BufferSamples!=2048)throw new InvalidOperationException("BufferSamples must be 0 (automatic), 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, or 2048");
                if(settings.LeftChannel<0||settings.RightChannel<0||settings.LeftChannel==settings.RightChannel||string.IsNullOrWhiteSpace(settings.Driver)||settings.Driver.Length>126)throw new InvalidOperationException("Invalid ASIO driver or output channels");
                var first=Assembly.Load("Assembly-CSharp-firstpass");var game=Assembly.Load("Assembly-CSharp");
                if(FileHash(first.Location)!=Compatibility.CriBridgeHash)throw new InvalidOperationException("Unsupported managed CRI bridge: Assembly-CSharp-firstpass.dll does not match the validated profile");
                var atom=first.GetType("CriAtomPlugin",false);
                var wasapi=game.GetType("Manager.WasapiExclusive",false);
                // Check every managed hook before installing any of them. Localized game text
                // can change Assembly-CSharp without changing this audio contract.
                Compatibility.Validate(atom,wasapi);
                bufferSamples=settings.BufferSamples;
                setConfig=AccessTools.Method(atom,"SetConfigParameters");
                isInitialized=AccessTools.Method(atom,"IsLibraryInitialized");
                Patch(atom,"SetConfigParameters","Configure","Configured");
                Patch(atom,"InitializeLibrary","BeforeInitialize","AfterInitialize");
                Patch(atom,"FinalizeLibrary","BeforeFinalize","AfterFinalize");
                Patch(wasapi,"Intialize","SkipWasapi",null);
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
            __9=48000;__10=2;__12=Configuration.ProcessingFrequency(bufferSamples,serverFrequencyOverride);
            Diagnostic("CRI configuration: 48000 Hz, 2 channels, serverFrequency="+__12);
        }
        static void Configured(object[] __args) {
            if(enabled&&!failed)configArguments=(object[])__args.Clone();
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
                    var options=new Options {Size=(uint)Marshal.SizeOf(typeof(Options)),Abi=Native.Abi,Frames=(uint)settings.BufferSamples,Left=(uint)settings.LeftChannel,Right=(uint)settings.RightChannel,Driver=settings.Driver,Source=source,SoftLimiter=settings.SoftLimiter?1u:0u};
                    nativeAttempted=true;Native.Check(Native.SaInitialize(ref options));prepared=true;
                    if(settings.CaptureOutput&&source!=3) {
                        try {Native.Check(Native.SaConfigureCapture(30));captureEnabled=true;}
                        catch(Exception ex){MelonLogger.Warning("[SinmaiAsio] Capture disabled / 录音已禁用: "+ex.GetBaseException().Message);}
                    }
                    bufferSamples=checked((int)options.Frames);
                    if(bufferSamples<=0)throw new InvalidOperationException("Native backend did not return an effective buffer size");
                    if(configArguments==null)throw new InvalidOperationException("CRI configuration was not captured before initialization");
                    // Reapply the captured settings only after safely loading ASIO, but before CRI
                    // initialization. Replay through Harmony to retain other mods' config patches.
                    var arguments=(object[])configArguments.Clone();arguments[12]=Configuration.ProcessingFrequency(bufferSamples,serverFrequencyOverride);
                    setConfig.Invoke(null,arguments);
                    var info=new StringBuilder(2048);Native.SaGetDriverInfo(info,2048);
                    Diagnostic("Prepared source="+source+" block="+bufferSamples+" ABI sizes="+Marshal.SizeOf(typeof(Options))+"/"+Marshal.SizeOf(typeof(Stats))+" amdaemonBase=0x"+daemon.ToInt64().ToString("X")+" "+info);
                }
                if(!installed){Native.Check(Native.SaInstallCri());installed=true;Diagnostic("Native output adapter installed");}
                return true;
            }catch(Exception ex){Failure(ex);return false;}
        }
        static void AfterInitialize() {
            if(!enabled||failed||started)return;
            try {Native.Check(Native.SaAfterCriInitialize());Native.Check(Native.SaStart());started=true;nextSnapshot=DateTime.UtcNow;MelonLogger.Msg("[SinmaiAsio] ASIO started: "+settings.Driver+", 48000 Hz, "+bufferSamples+" samples");}
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
            failed=true;
            if(ex is ConfigurationFormatException) {
                problem=ex.Message;
                MelonLogger.Warning("[SinmaiAsio] "+problem);
            } else {
                problem="Sinmai ASIO 音频已停止，请退出游戏并检查日志。\nAudio stopped. Exit the game and inspect the log.\n"+ex.GetBaseException().Message;
                MelonLogger.Error("[SinmaiAsio] "+problem+"\n"+ex);
            }
            if(nativeAttempted){try{Native.SaStop();}catch{}}
        }
        static void SaveOutputCapture() {
            if(Interlocked.CompareExchange(ref captureBusy,1,0)!=0)return;
            try {
                string directory=Path.Combine(gameRoot,"UserData");Directory.CreateDirectory(directory);
                string path=Path.Combine(directory,"SinmaiAsio-Capture-"+DateTime.Now.ToString("yyyyMMdd-HHmmss-fff")+"-"+Guid.NewGuid().ToString("N").Substring(0,6)+".wav");
                string statistics=Interlocked.CompareExchange(ref latestStatistics,null,null);
                ThreadPool.QueueUserWorkItem(_=>{
                    try {
                        Native.Check(Native.SaSaveCapture(path));
                        if(statistics!=null)File.WriteAllText(path+".stats.json",statistics);
                        Interlocked.Exchange(ref captureNotice,"[SinmaiAsio] PCM capture saved / 录音已保存: "+path);
                    }catch(Exception ex){Interlocked.Exchange(ref captureNotice,"[SinmaiAsio] Capture failed / 录音失败: "+ex.GetBaseException().Message);}
                    finally {Interlocked.Exchange(ref captureBusy,0);}
                });
            }catch(Exception ex){Interlocked.Exchange(ref captureBusy,0);Interlocked.Exchange(ref captureNotice,"[SinmaiAsio] Capture failed / 录音失败: "+ex.GetBaseException().Message);}
        }
        public override void OnUpdate() {
            string notice=Interlocked.Exchange(ref captureNotice,null);
            if(notice!=null)MelonLogger.Msg(notice);
            if(!enabled||!prepared||DateTime.UtcNow<nextSnapshot)return;
            nextSnapshot=DateTime.UtcNow.AddSeconds(5);
            try {
                var s=Stats.Create();Native.Check(Native.SaGetStats(ref s));
                if(s.Fault!=0&&!failed)Failure(new InvalidOperationException(FaultReason(s.Fault)));
                if(settings.Diagnostics) {
                    string json="{\"schema\":1,\"source\":"+source+",\"state\":"+s.State+",\"fault\":"+s.Fault+",\"callbacks\":"+s.Callbacks+",\"frames\":"+s.Frames+",\"missingSamplesIncludingWarmup\":"+s.Missing+",\"criCalls\":"+s.CriCalls+",\"criFrames\":"+s.CriFrames+",\"nonzeroFrames\":"+s.Nonzero+",\"queue\":"+s.Queued+",\"peakQueue\":"+s.PeakQueue+",\"criThread\":"+s.CriThread+",\"asioThread\":"+s.AsioThread+",\"threadChanges\":"+s.ThreadChanges+",\"channels\":"+s.Channels+",\"minBlock\":"+s.MinimumBlock+",\"maxBlock\":"+s.MaximumBlock+",\"elapsedSeconds\":"+Number(s.ElapsedSeconds)+",\"reportedOutputMs\":"+Number(s.ReportedOutputMs)+",\"maxCallbackUs\":"+Number(s.MaxCallbackUs)+",\"waitHistogram250us\":["+string.Join(",",s.WaitHistogram)+"],\"criGapHistogram250us\":["+string.Join(",",s.GapHistogram)+"]}";
                    json=json.Substring(0,json.Length-1)+",\"renderCalls\":"+s.RenderCalls+",\"renderFrames\":"+s.RenderFrames+"}";
                    json=json.Substring(0,json.Length-1)+",\"nativeAbi\":"+Native.Abi+",\"missingAfterWarmup\":"+s.MissingAfterWarmup+",\"outputNonzeroFrames\":"+s.OutputNonzero+",\"maximumRenderLockUs\":"+Number(s.MaximumRenderLockUs)+",\"maximumRenderWorkUs\":"+Number(s.MaximumRenderWorkUs)+"}";
                    json=json.Substring(0,json.Length-1)+",\"inputPeak\":"+Number(s.InputPeak)+",\"overRangeFrames\":"+s.OverRangeFrames+",\"limitedFrames\":"+s.LimitedFrames+",\"invalidSamples\":"+s.InvalidSamples+"}";
                    json=json.Substring(0,json.Length-1)+",\"callbackMaxGapUs\":"+Number(s.CallbackMaxGapUs)+",\"callbackMaxGapExcessUs\":"+Number(s.CallbackMaxGapExcessUs)+",\"callbackGapCount\":"+s.CallbackGapCount+",\"callbackGapsOver150Percent\":"+s.CallbackGapsOver150Percent+",\"callbackGapsOver200Percent\":"+s.CallbackGapsOver200Percent+"}";
                    json=json.Substring(0,json.Length-1)+",\"timeInfoCallbacks\":"+s.TimeInfoCallbacks+",\"legacyCallbacks\":"+s.LegacyCallbacks+",\"validPositionCallbacks\":"+s.ValidPositionCallbacks+",\"unavailablePositionCallbacks\":"+s.UnavailablePositionCallbacks+",\"positionDiscontinuities\":"+s.PositionDiscontinuities+",\"positionForwardGapSamples\":"+s.PositionForwardGapSamples+",\"positionBackwardSteps\":"+s.PositionBackwardSteps+",\"positionRepeatedSteps\":"+s.PositionRepeatedSteps+",\"positionShortSteps\":"+s.PositionShortSteps+"}";
                    json=json.Substring(0,json.Length-1)+",\"resyncRequests\":"+s.ResyncRequests+",\"resetRequests\":"+s.ResetRequests+",\"bufferChangeRequests\":"+s.BufferChangeRequests+",\"latencyChangeRequests\":"+s.LatencyChangeRequests+"}";
                    json=json.Substring(0,json.Length-1)+",\"bufferSamples\":"+bufferSamples+",\"configuredServerFrequency\":"+Number(Configuration.ProcessingFrequency(bufferSamples,serverFrequencyOverride))+",\"serverFrequencyOverride\":"+Number(serverFrequencyOverride)+"}";
                    Interlocked.Exchange(ref latestStatistics,json);
                    Directory.CreateDirectory(Path.GetDirectoryName(statisticsPath));File.WriteAllText(statisticsPath,json);
                    MelonLogger.Msg("[SinmaiAsio] source="+source+" state="+s.State+" fault="+s.Fault+" cri="+s.CriFrames+" audible="+s.Nonzero+" callbacks="+s.Callbacks+" missing="+s.Missing+" queue="+s.Queued+" peak="+s.PeakQueue);
                }
                if(captureEnabled&&started&&!failed&&Volatile.Read(ref captureBusy)==0) {
                    string request=Path.Combine(gameRoot,"UserData","SinmaiAsio.capture");
                    try {if(File.Exists(request)){File.Delete(request);SaveOutputCapture();}}
                    catch(Exception ex){MelonLogger.Warning("[SinmaiAsio] Capture request failed / 录音请求失败: "+ex.GetBaseException().Message);}
                }
            }catch(Exception ex){Failure(ex);}
        }
        static string Number(double x){return x.ToString("R",System.Globalization.CultureInfo.InvariantCulture);}
        static void Diagnostic(string message){if(settings!=null&&settings.Diagnostics)MelonLogger.Msg("[SinmaiAsio] "+message);}
        static string FaultReason(uint code){
            switch(code){
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
