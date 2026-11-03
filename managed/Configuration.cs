// SPDX-License-Identifier: GPL-3.0-only
using System;
using System.IO;
using System.Globalization;
using System.Linq;
using System.Reflection;
using System.Text;
using Microsoft.Win32;
using Tomlet;
namespace SinmaiAsio {
    public sealed class Settings {
        public bool Enabled=false;
        public string Driver="Yamaha Steinberg USB ASIO";
        public int BufferSamples=0;
        public int LeftChannel=0;
        public int RightChannel=1;
        public bool Diagnostics=false;
        public bool CaptureOutput=false;
        public bool SoftLimiter=false;
    }
    internal sealed class ConfigurationFormatException:FormatException {
        internal ConfigurationFormatException(string path,Exception cause):base(
            "SinmaiAsio.toml 格式或选项类型有误，无法加载 ASIO 配置。请修正后重启游戏，原文件已保留。\n"+
            "Invalid TOML syntax or option type. Fix the configuration and restart; the original file was preserved.\n"+
            "配置文件 / File: "+path+"\n详细原因 / Details: "+cause.Message,cause) {}
        internal static ConfigurationFormatException FromToml(string path,Exception error) {
            for(var current=error;current!=null;current=current.InnerException)
                if(current is Tomlet.Exceptions.TomlException)return new ConfigurationFormatException(path,current);
            return null;
        }
    }
    internal static class Configuration {
        internal static float ParseServerFrequencyOverride(string value) {
            if(string.IsNullOrWhiteSpace(value))return 0;
            float frequency;
            if(!float.TryParse(value,NumberStyles.Float,CultureInfo.InvariantCulture,out frequency)||float.IsNaN(frequency)||float.IsInfinity(frequency)||frequency<16||frequency>1500)
                throw new InvalidOperationException("SINMAI_ASIO_SERVER_FREQUENCY must be a finite value from 16 to 1500 Hz");
            return frequency;
        }
        internal static float ProcessingFrequency(int frames,float diagnosticOverride) {
            // Commands and decoding must keep up independently of the ASIO block size.
            // Keep the tested 750-Hz floor without slowing smaller-buffer configurations.
            return diagnosticOverride>0?diagnosticOverride:Math.Max(750f,48000f/(frames>0?frames:128));
        }
        internal static string[] InstalledDrivers() {
            using(var registry=RegistryKey.OpenBaseKey(RegistryHive.LocalMachine,RegistryView.Registry64))
            using(var asio=registry.OpenSubKey(@"SOFTWARE\ASIO")) {
                if(asio==null)return new string[0];
                return asio.GetSubKeyNames().Where(name=>!string.IsNullOrWhiteSpace(name)).OrderBy(name=>name,StringComparer.OrdinalIgnoreCase).ToArray();
            }
        }
        internal static Settings LoadOrCreate(string path,Func<string[]> discover,out bool created) {
            created=false;
            if(File.Exists(path))return LoadExisting(path);
            var drivers=discover();
            string selected=drivers.FirstOrDefault(name=>string.Equals(name,"Yamaha Steinberg USB ASIO",StringComparison.OrdinalIgnoreCase));
            if(selected==null&&drivers.Length==1)selected=drivers[0];
            var settings=new Settings {Enabled=selected!=null,Driver=selected??""};
            string text="# BufferSamples = 0：启动时自动读取 ASIO 驱动的首选缓冲大小。\r\n"+
                "# BufferSamples = 0: use the ASIO driver preferred buffer size at startup.\r\n";
            if(selected==null)text+="# 未找到唯一驱动：请填写 Driver 并设置 Enabled = true。\r\n# No unambiguous driver: set Driver and Enabled = true manually.\r\n";
            text+="Enabled = "+(settings.Enabled?"true":"false")+"\r\nDriver = \""+Escape(settings.Driver)+"\"\r\nBufferSamples = 0\r\nLeftChannel = 0\r\nRightChannel = 1\r\nDiagnostics = false\r\nCaptureOutput = false\r\nSoftLimiter = false\r\n";
            // CreateNew prevents overwriting a configuration created concurrently.
            using(var stream=new FileStream(path,FileMode.CreateNew,FileAccess.Write,FileShare.None))
            using(var writer=new StreamWriter(stream,new UTF8Encoding(false)))writer.Write(text);
            created=true;
            return settings;
        }
        static Settings LoadExisting(string path) {
            string original=File.ReadAllText(path);
            // Validate before modifying anything. Inspect parsed root keys, not text matches:
            // comments, quoted keys, multiline strings and nested tables must stay unambiguous.
            Settings settings;
            Tomlet.Models.TomlDocument document;
            try {
                settings=Read(original);
                document=new TomlParser().Parse(original);
            }catch(Exception ex) {
                var format=ConfigurationFormatException.FromToml(path,ex);
                if(format!=null)throw format;
                throw;
            }
            var defaults=new Settings();
            string newline=original.Contains("\r\n")?"\r\n":"\n";
            var missing=new StringBuilder();
            foreach(var field in typeof(Settings).GetFields(BindingFlags.Public|BindingFlags.Instance).OrderBy(f=>f.MetadataToken)) {
                if(document.ContainsKey(field.Name))continue;
                object value=field.GetValue(defaults);
                string encoded;
                if(value is bool)encoded=(bool)value?"true":"false";
                else if(value is int)encoded=((int)value).ToString(CultureInfo.InvariantCulture);
                else if(value is string)encoded="\""+Escape((string)value)+"\"";
                else throw new InvalidOperationException("Unsupported default config field: "+field.Name);
                missing.Append(field.Name).Append(" = ").Append(encoded).Append(newline);
            }
            if(missing.Length==0)return settings;
            // Prepend root keys so an existing final [table] cannot capture the additions.
            // Existing text (including comments and unknown keys) is retained verbatim.
            string completed=missing.ToString()+newline+original;
            Read(completed);
            string temporary=path+".tmp-"+Guid.NewGuid().ToString("N");
            try {
                File.WriteAllText(temporary,completed,new UTF8Encoding(false));
                if(File.ReadAllText(path)!=original)throw new IOException("Configuration changed while adding missing options; restart to retry");
                File.Replace(temporary,path,null);
            } finally {if(File.Exists(temporary))File.Delete(temporary);}
            return settings;
        }
        static string Escape(string value) {
            var text=new StringBuilder();
            foreach(char ch in value) {
                if(ch=='\\')text.Append("\\\\");
                else if(ch=='\"')text.Append("\\\"");
                else if(ch<0x20||ch==0x7f)text.Append("\\u"+((int)ch).ToString("X4"));
                else text.Append(ch);
            }
            return text.ToString();
        }
        internal static Settings Read(string text) {
            var method=typeof(TomletMain).GetMethods(BindingFlags.Static|BindingFlags.Public).First(m=>m.Name=="To"&&m.IsGenericMethodDefinition&&m.GetParameters().Length>=1&&m.GetParameters()[0].ParameterType==typeof(string));
            var args=new object[method.GetParameters().Length];args[0]=text;
            return (Settings)method.MakeGenericMethod(typeof(Settings)).Invoke(null,args);
        }
    }
}