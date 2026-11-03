// SPDX-License-Identifier: GPL-3.0-only
using System;
using System.IO;
using System.Runtime.InteropServices;
using SinmaiAsio;
static class ConfigurationTests {
    static void Check(bool condition,string message){if(!condition)throw new Exception(message);}
    static int Main(string[] args){
        CompatibilityTests.Run();
        if(args.Length==1)CompatibilityTests.CheckInstalled(args[0]);
        Check(Native.Abi==7,"Unexpected native ABI");
        Check(Marshal.SizeOf(typeof(Options))==284&&Marshal.SizeOf(typeof(Stats))==1392,"Native ABI struct size mismatch");
        Check(Marshal.OffsetOf(typeof(Stats),"InputPeak").ToInt64()==1216&&Marshal.OffsetOf(typeof(Stats),"InvalidSamples").ToInt64()==1240,"Peak meter field offset mismatch");
        Check(Marshal.OffsetOf(typeof(Stats),"CallbackMaxGapUs").ToInt64()==1248&&Marshal.OffsetOf(typeof(Stats),"LatencyChangeRequests").ToInt64()==1384,"Timing meter field offset mismatch");
        Check(Marshal.OffsetOf(typeof(Options),"SoftLimiter").ToInt64()==280,"Limiter option offset mismatch");
        Check(Configuration.ParseServerFrequencyOverride(null)==0,"Missing diagnostic override must use the default");
        foreach(int frames in new[]{0,96,128,192,256,384,512,768,1024,1536,2048})
            Check(Configuration.ProcessingFrequency(frames,0)==750f,"CRI processing cadence fell below the tested floor: "+frames);
        Check(Configuration.ProcessingFrequency(64,0)==750f&&Configuration.ProcessingFrequency(48,0)==1000f&&Configuration.ProcessingFrequency(32,0)==1500f,"Small-buffer processing cadence changed");
        Check(Configuration.ProcessingFrequency(256,187.5f)==187.5f,"Diagnostic override must allow the old cadence for comparisons");
        Check(Configuration.ProcessingFrequency(256,Configuration.ParseServerFrequencyOverride("750"))==750,"Diagnostic frequency override failed");
        Check(Configuration.ParseServerFrequencyOverride("187.5")==187.5f,"Invariant diagnostic frequency parsing failed");
        foreach(string invalid in new[]{"NaN","Infinity","-1","0","15","1501","bad"}) {
            bool rejected=false;try{Configuration.ParseServerFrequencyOverride(invalid);}catch(InvalidOperationException){rejected=true;}
            Check(rejected,"Invalid diagnostic frequency was accepted: "+invalid);
        }
        string root=Path.Combine(Path.GetTempPath(),"sinmai-asio-config-"+Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try {
            bool created;
            string path=Path.Combine(root,"single.toml");
            string driver="Driver \\\"中文\t";
            var settings=Configuration.LoadOrCreate(path,()=>new[]{driver},out created);
            Check(created&&settings.Enabled&&settings.Driver==driver,"Single driver selection failed");
            var parsed=Configuration.Read(File.ReadAllText(path));
            Check(parsed.Enabled&&parsed.Driver==driver&&parsed.BufferSamples==0&&!parsed.Diagnostics&&!parsed.CaptureOutput&&!parsed.SoftLimiter,"Generated TOML round trip failed");
            byte[] original=File.ReadAllBytes(path);
            settings=Configuration.LoadOrCreate(path,()=>{throw new Exception("Existing config must not discover drivers");},out created);
            Check(!created&&Convert.ToBase64String(original)==Convert.ToBase64String(File.ReadAllBytes(path)),"Existing config changed");
            settings=Configuration.LoadOrCreate(Path.Combine(root,"preferred.toml"),()=>new[]{"Other","yamaha steinberg usb asio"},out created);
            Check(settings.Enabled&&settings.Driver=="yamaha steinberg usb asio","Preferred driver selection failed");
            settings=Configuration.LoadOrCreate(Path.Combine(root,"none.toml"),()=>new string[0],out created);
            Check(!settings.Enabled&&settings.Driver=="","Missing drivers must remain disabled");
            settings=Configuration.LoadOrCreate(Path.Combine(root,"multiple.toml"),()=>new[]{"First","Second"},out created);
            Check(!settings.Enabled&&settings.Driver=="","Ambiguous drivers must remain disabled");
            path=Path.Combine(root,"disabled.toml");
            File.WriteAllText(path,"Enabled = false\nDriver = \"Chosen\"\nBufferSamples = 64\n");
            settings=Configuration.LoadOrCreate(path,()=>{throw new Exception("Disabled config must be preserved");},out created);
            Check(!created&&!settings.Enabled&&settings.Driver=="Chosen"&&settings.BufferSamples==64,"User configuration not preserved");
            string preserved=File.ReadAllText(path);
            Check(preserved.Contains("SoftLimiter = false")&&preserved.Contains("CaptureOutput = false"),"Missing defaults were not persisted");
            original=File.ReadAllBytes(path);
            Configuration.LoadOrCreate(path,()=>{throw new Exception("Completion must not rediscover drivers");},out created);
            Check(Convert.ToBase64String(original)==Convert.ToBase64String(File.ReadAllBytes(path)),"Completion is not idempotent");
            path=Path.Combine(root,"comments-and-tables.toml");
            string oldText="# 用户配置 / user comment\nEnabled = true # keep enabled\nDriver = \"Chosen\"\nBufferSamples = 256\n\"SoftLimiter\" = true # keep enabled limiter\nFutureOption = \"\"\"\nCaptureOutput = true\n\"\"\"\n[Other]\nDiagnostics = true\n";
            File.WriteAllText(path,oldText);
            settings=Configuration.LoadOrCreate(path,()=>{throw new Exception("Existing values must be preserved");},out created);
            string completed=File.ReadAllText(path);
            Check(!created&&settings.Enabled&&settings.Driver=="Chosen"&&settings.BufferSamples==256&&settings.SoftLimiter,"Existing explicit values changed");
            Check(!settings.CaptureOutput&&!settings.Diagnostics,"Nested or multiline text was treated as a root setting");
            Check(completed.EndsWith(oldText)&&completed.StartsWith("LeftChannel = 0\n"),"Original comments, unknown keys, table or newline style changed");
            Check(completed.IndexOf("SoftLimiter = false",StringComparison.Ordinal)<0,"Quoted existing key was duplicated");
            var completedSettings=Configuration.Read(completed);
            Check(!completedSettings.CaptureOutput&&!completedSettings.Diagnostics&&completedSettings.SoftLimiter,"Added keys were placed inside a table");
            path=Path.Combine(root,"empty.toml");File.WriteAllText(path,"");
            settings=Configuration.LoadOrCreate(path,()=>{throw new Exception("Empty existing config must use safe defaults");},out created);
            Check(!settings.Enabled&&!settings.SoftLimiter&&File.ReadAllText(path).Contains("Enabled = false"),"Empty config completion failed");
            path=Path.Combine(root,"invalid.toml");File.WriteAllText(path,"Enabled = [");
            bool rejected=false;try{Configuration.LoadOrCreate(path,()=>new[]{"Other"},out created);}catch(ConfigurationFormatException ex){rejected=ex.Message.Contains(path)&&ex.Message.Contains("格式")&&ex.Message.Contains("Invalid TOML")&&ex.InnerException!=null;}
            Check(rejected&&File.ReadAllText(path)=="Enabled = [","Invalid config must fail without overwrite");
            path=Path.Combine(root,"wrong-type.toml");string invalidType="Enabled = \"yes\"\n";File.WriteAllText(path,invalidType);
            rejected=false;try{Configuration.LoadOrCreate(path,()=>new[]{"Other"},out created);}catch(ConfigurationFormatException ex){rejected=ex.Message.Contains(path)&&ex.Message.Contains("选项类型");}
            Check(rejected&&File.ReadAllText(path)==invalidType,"Type mismatch must provide the same config warning without overwrite");
            Check(Configuration.Read("Enabled = true").BufferSamples==0,"Omitted buffer setting must default to automatic");
            Check(Configuration.Read("CaptureOutput = true").CaptureOutput,"Capture opt-in was not parsed");
            Check(Configuration.Read("SoftLimiter = true").SoftLimiter,"Soft limiter opt-in was not parsed");
            Check(!Configuration.Read("Enabled = true").SoftLimiter,"Omitted soft limiter must remain disabled");
            Console.WriteLine("Configuration tests passed (selection, escaping, default completion, preservation, idempotence, invalid input).");
            return 0;
        } finally {
            foreach(string file in Directory.GetFiles(root))File.Delete(file);
            Directory.Delete(root);
        }
    }
}