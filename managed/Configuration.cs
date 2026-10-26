// SPDX-License-Identifier: GPL-3.0-only
using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using Microsoft.Win32;
using Tomlet;
namespace SinmaiAsio {
    public sealed class Settings {
        public bool Enabled=false;
        public string Driver="Yamaha Steinberg USB ASIO";
        public int BufferSamples=128;
        public int LeftChannel=0;
        public int RightChannel=1;
        public bool Diagnostics=false;
    }
    internal static class Configuration {
        internal static string[] InstalledDrivers() {
            using(var registry=RegistryKey.OpenBaseKey(RegistryHive.LocalMachine,RegistryView.Registry64))
            using(var asio=registry.OpenSubKey(@"SOFTWARE\ASIO")) {
                if(asio==null)return new string[0];
                return asio.GetSubKeyNames().Where(name=>!string.IsNullOrWhiteSpace(name)).OrderBy(name=>name,StringComparer.OrdinalIgnoreCase).ToArray();
            }
        }
        internal static Settings LoadOrCreate(string path,Func<string[]> discover,out bool created) {
            created=false;
            if(File.Exists(path))return Read(File.ReadAllText(path));
            var drivers=discover();
            string selected=drivers.FirstOrDefault(name=>string.Equals(name,"Yamaha Steinberg USB ASIO",StringComparison.OrdinalIgnoreCase));
            if(selected==null&&drivers.Length==1)selected=drivers[0];
            var settings=new Settings {Enabled=selected!=null,Driver=selected??""};
            string text="# 驱动面板须设置 48000 Hz，并与 BufferSamples 一致。\r\n"+
                "# Set the driver panel to 48000 Hz and match BufferSamples.\r\n";
            if(selected==null)text+="# 未找到唯一驱动：请填写 Driver 并设置 Enabled = true。\r\n# No unambiguous driver: set Driver and Enabled = true manually.\r\n";
            text+="Enabled = "+(settings.Enabled?"true":"false")+"\r\nDriver = \""+Escape(settings.Driver)+"\"\r\nBufferSamples = 128\r\nLeftChannel = 0\r\nRightChannel = 1\r\nDiagnostics = false\r\n";
            // CreateNew prevents overwriting a configuration created concurrently.
            using(var stream=new FileStream(path,FileMode.CreateNew,FileAccess.Write,FileShare.None))
            using(var writer=new StreamWriter(stream,new UTF8Encoding(false)))writer.Write(text);
            created=true;
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