using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;

namespace Zlink.Native;

internal static class NativeLibraryLoader
{
    private static readonly object Sync = new();
    private static IntPtr _handle = IntPtr.Zero;
    private static bool _resolverInstalled;

    internal static void EnsureLoaded()
    {
        EnsureResolverInstalled();
        if (_handle != IntPtr.Zero)
            return;

        lock (Sync)
        {
            if (_handle != IntPtr.Zero)
                return;

            if (TryLoadConfiguredPath(out _handle))
                return;
            if (TryLoadWellKnownNames(out _handle))
                return;

            string rid = GetRid();
            foreach (string baseDir in GetBaseDirs())
            {
                foreach (string candidate in GetCandidates(baseDir, rid))
                {
                    if (!File.Exists(candidate))
                        continue;
                    if (TryLoad(candidate, out _handle))
                        return;
                }
            }
        }
    }

    private static void EnsureResolverInstalled()
    {
        if (_resolverInstalled)
            return;

        lock (Sync)
        {
            if (_resolverInstalled)
                return;
            NativeLibrary.SetDllImportResolver(typeof(NativeLibraryLoader).Assembly,
                Resolve);
            _resolverInstalled = true;
        }
    }

    private static IntPtr Resolve(string libraryName, Assembly assembly,
        DllImportSearchPath? searchPath)
    {
        if (!string.Equals(libraryName, "zlink", StringComparison.Ordinal)
            && !string.Equals(libraryName, "libzlink", StringComparison.Ordinal))
        {
            return IntPtr.Zero;
        }
        EnsureLoaded();
        return _handle;
    }

    private static bool TryLoadConfiguredPath(out IntPtr handle)
    {
        handle = IntPtr.Zero;
        string? path = Environment.GetEnvironmentVariable("ZLINK_LIBRARY_PATH");
        if (string.IsNullOrWhiteSpace(path))
            return false;
        return TryLoad(path, out handle);
    }

    private static bool TryLoadWellKnownNames(out IntPtr handle)
    {
        foreach (string name in GetWellKnownNames())
        {
            if (TryLoad(name, out handle))
                return true;
        }
        handle = IntPtr.Zero;
        return false;
    }

    private static bool TryLoad(string nameOrPath, out IntPtr handle)
    {
        try
        {
            handle = NativeLibrary.Load(nameOrPath);
            return true;
        }
        catch
        {
            handle = IntPtr.Zero;
            return false;
        }
    }

    private static IEnumerable<string> GetCandidates(string baseDir, string rid)
    {
        string[] libNames = GetLibNames();
        foreach (string libName in libNames)
        {
            yield return Path.Combine(baseDir, "runtimes", rid, "native", libName);
            yield return Path.Combine(baseDir, rid, "native", libName);
            yield return Path.Combine(baseDir, "native", libName);
            yield return Path.Combine(baseDir, libName);
        }
    }

    private static IEnumerable<string> GetBaseDirs()
    {
        var seen = new HashSet<string>(StringComparer.Ordinal);
        string appBase = AppContext.BaseDirectory;
        if (!string.IsNullOrEmpty(appBase) && seen.Add(appBase))
            yield return appBase;

        string? assemblyBase =
          Path.GetDirectoryName(typeof(NativeLibraryLoader).Assembly.Location);
        if (!string.IsNullOrEmpty(assemblyBase) && seen.Add(assemblyBase))
            yield return assemblyBase;

        string? entryBase = Path.GetDirectoryName(Assembly.GetEntryAssembly()?.Location);
        if (!string.IsNullOrEmpty(entryBase) && seen.Add(entryBase))
            yield return entryBase;
    }

    private static string[] GetWellKnownNames()
    {
        if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
            return new[] { "zlink", "zlink.dll" };
        if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
            return new[] { "zlink", "libzlink.dylib" };
        return new[] { "zlink", "libzlink.so", "libzlink.so.5" };
    }

    private static string GetRid()
    {
        string arch = RuntimeInformation.ProcessArchitecture switch
        {
            Architecture.Arm64 => "arm64",
            Architecture.X64 => "x64",
            Architecture.X86 => "x86",
            _ => RuntimeInformation.ProcessArchitecture.ToString().ToLowerInvariant()
        };
        if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
            return $"win-{arch}";
        if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
            return $"osx-{arch}";
        return $"linux-{arch}";
    }

    private static string[] GetLibNames()
    {
        if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
            return new[] { "zlink.dll" };
        if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
            return new[] { "libzlink.dylib" };
        return new[] { "libzlink.so", "libzlink.so.5" };
    }
}
