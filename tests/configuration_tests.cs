// SPDX-License-Identifier: GPL-3.0-only
using System;
using System.IO;
using SinmaiAsio;
static class ConfigurationTests {
    static void Check(bool condition,string message){if(!condition)throw new Exception(message);}
    static int Main(){
        string root=Path.Combine(Path.GetTempPath(),"sinmai-asio-config-"+Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try {
            bool created;
            string path=Path.Combine(root,"single.toml");
            string driver="Driver \\\"中文\t";
            var settings=Configuration.LoadOrCreate(path,()=>new[]{driver},out created);
            Check(created&&settings.Enabled&&settings.Driver==driver,"Single driver selection failed");
            var parsed=Configuration.Read(File.ReadAllText(path));
            Check(parsed.Enabled&&parsed.Driver==driver&&parsed.BufferSamples==128&&!parsed.Diagnostics,"Generated TOML round trip failed");
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
            path=Path.Combine(root,"invalid.toml");File.WriteAllText(path,"Enabled = [");
            bool rejected=false;try{Configuration.LoadOrCreate(path,()=>new[]{"Other"},out created);}catch{rejected=true;}
            Check(rejected&&File.ReadAllText(path)=="Enabled = [","Invalid config must fail without overwrite");
            Console.WriteLine("Configuration tests passed (selection, TOML escaping, preservation, invalid input).");
            return 0;
        } finally {
            foreach(string file in Directory.GetFiles(root))File.Delete(file);
            Directory.Delete(root);
        }
    }
}