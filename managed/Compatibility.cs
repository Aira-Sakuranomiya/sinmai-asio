// SPDX-License-Identifier: GPL-3.0-only
using System;
using System.Linq;
using System.Reflection;
namespace SinmaiAsio {
    internal static class Compatibility {
        // Native offsets are validated separately against the pinned CRI binary.
        // Pin the managed CRI bridge too: its implementation defines configuration and ref-counting.
        internal const string CriBridgeHash="E032F8A6777AB5C6EBC4FFAFD0DAD2A6C5C808FC00DB3A5F717CBBAA0D80DA3E";
        internal static void Validate(Type atom,Type wasapi) {
            if(atom==null||wasapi==null)throw new InvalidOperationException("Unsupported audio interface: CriAtomPlugin or Manager.WasapiExclusive is missing");
            RequireMethod(atom,"SetConfigParameters",typeof(void),new[]{typeof(int),typeof(int),typeof(int),typeof(int),typeof(int),typeof(int),typeof(int),typeof(int),typeof(int),typeof(int),typeof(int),typeof(bool),typeof(float),typeof(int),typeof(int),typeof(int),typeof(bool)});
            RequireMethod(atom,"InitializeLibrary",typeof(void),Type.EmptyTypes);
            RequireMethod(atom,"FinalizeLibrary",typeof(void),Type.EmptyTypes);
            RequireMethod(atom,"IsLibraryInitialized",typeof(bool),Type.EmptyTypes);
            var count=atom.GetField("initializationCount",BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Static|BindingFlags.Instance);
            if(count==null||!count.IsStatic||count.FieldType!=typeof(int))throw new InvalidOperationException("Unsupported audio interface: CriAtomPlugin.initializationCount must be static Int32");
            RequireMethod(wasapi,"Intialize",typeof(void),Type.EmptyTypes);
        }
        internal static MethodInfo RequireMethod(Type type,string name,Type result,Type[] parameters) {
            var methods=type.GetMethods(BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Static|BindingFlags.Instance|BindingFlags.DeclaredOnly).Where(m=>m.Name==name).ToArray();
            if(methods.Length!=1||!methods[0].IsStatic||methods[0].ContainsGenericParameters||methods[0].ReturnType!=result||!methods[0].GetParameters().Select(p=>p.ParameterType).SequenceEqual(parameters)||methods[0].GetMethodBody()==null)
                throw new InvalidOperationException("Unsupported audio interface: "+type.FullName+"."+name+" signature or managed body does not match");
            return methods[0];
        }
    }
}
