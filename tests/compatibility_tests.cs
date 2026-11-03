// SPDX-License-Identifier: GPL-3.0-only
using System;
using System.Reflection;
using System.IO;
using System.Security.Cryptography;
using System.Reflection.Emit;
using SinmaiAsio;
static class CompatibilityTests {
    static Type Fixture(bool atom,string broken) {
        var assembly=AppDomain.CurrentDomain.DefineDynamicAssembly(new AssemblyName("AudioFixture"+Guid.NewGuid().ToString("N")),AssemblyBuilderAccess.Run);
        var type=assembly.DefineDynamicModule("fixture").DefineType(atom?"CriAtomPlugin":"Manager.WasapiExclusive",TypeAttributes.Public);
        if(atom)type.DefineField("initializationCount",broken=="field-type"?typeof(long):typeof(int),FieldAttributes.Private|(broken=="instance-field"?0:FieldAttributes.Static));
        foreach(string name in atom?new[]{"SetConfigParameters","InitializeLibrary","FinalizeLibrary","IsLibraryInitialized"}:new[]{"Intialize"}) {
            if(broken=="missing-"+name)continue;
            Type[] args=name=="SetConfigParameters"?new[]{typeof(int),typeof(int),typeof(int),typeof(int),typeof(int),typeof(int),typeof(int),typeof(int),typeof(int),typeof(int),typeof(int),typeof(bool),typeof(float),typeof(int),typeof(int),typeof(int),typeof(bool)}:Type.EmptyTypes;
            if(broken=="parameter"&&name=="SetConfigParameters")args[12]=typeof(double);
            bool returnsBool=name=="IsLibraryInitialized";
            var result=returnsBool?typeof(bool):typeof(void);
            if(broken=="return"&&name=="Intialize")result=typeof(bool);
            var method=type.DefineMethod(name,MethodAttributes.Public|(broken=="instance-method"?0:MethodAttributes.Static),result,args);
            var il=method.GetILGenerator();if(result==typeof(bool))il.Emit(OpCodes.Ldc_I4_0);il.Emit(OpCodes.Ret);
            if(broken=="overload"&&name=="Intialize") {var extra=type.DefineMethod(name,MethodAttributes.Public|MethodAttributes.Static,typeof(void),new[]{typeof(int)});extra.GetILGenerator().Emit(OpCodes.Ret);}
        }
        return type.CreateType();
    }
    internal static void CheckInstalled(string managed) {
        ResolveEventHandler resolver=(sender,args)=> {
            string name=new AssemblyName(args.Name).Name;
            if(name.EndsWith(".resources",StringComparison.Ordinal))return null;
            string path=Path.Combine(managed,name+".dll");
            return File.Exists(path)?Assembly.LoadFrom(path):null;
        };
        AppDomain.CurrentDomain.AssemblyResolve+=resolver;
        try {
            string bridge=Path.Combine(managed,"Assembly-CSharp-firstpass.dll");
            using(var sha=SHA256.Create())using(var stream=File.OpenRead(bridge))
                if(BitConverter.ToString(sha.ComputeHash(stream)).Replace("-","")!=Compatibility.CriBridgeHash)throw new Exception("Installed CRI bridge fingerprint mismatch");
            var first=Assembly.LoadFrom(bridge);
            var game=Assembly.LoadFrom(Path.Combine(managed,"Assembly-CSharp.dll"));
            Compatibility.Validate(first.GetType("CriAtomPlugin",true),game.GetType("Manager.WasapiExclusive",true));
            Console.WriteLine("Installed game audio interfaces and managed CRI fingerprint passed (no game methods executed).");
        } finally {AppDomain.CurrentDomain.AssemblyResolve-=resolver;}
    }
    internal static void Run() {
        var atom=Fixture(true,"");var game=Fixture(false,"");
        Compatibility.Validate(atom,game);
        // A different assembly identity (e.g. translated game text) is accepted with the same contract.
        Compatibility.Validate(atom,Fixture(false,""));
        foreach(string broken in new[]{"field-type","instance-field","parameter","instance-method","missing-InitializeLibrary","missing-FinalizeLibrary","missing-IsLibraryInitialized","missing-SetConfigParameters"}) {
            bool rejected=false;try{Compatibility.Validate(Fixture(true,broken),game);}catch(InvalidOperationException){rejected=true;}
            if(!rejected)throw new Exception("Unsafe CRI interface accepted: "+broken);
        }
        foreach(string broken in new[]{"return","instance-method","overload","missing-Intialize"}) {
            bool rejected=false;try{Compatibility.Validate(atom,Fixture(false,broken));}catch(InvalidOperationException){rejected=true;}
            if(!rejected)throw new Exception("Unsafe game interface accepted: "+broken);
        }
        bool missing=false;try{Compatibility.Validate(atom,null);}catch(InvalidOperationException){missing=true;}
        if(!missing)throw new Exception("Missing game audio type accepted");
        Console.WriteLine("Compatibility tests passed (localized assembly, signatures, fields, overloads and missing interfaces).");
    }
}
