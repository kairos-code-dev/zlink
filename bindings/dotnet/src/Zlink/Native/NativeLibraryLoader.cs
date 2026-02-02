using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;

namespace Zlink.Native;

internal static class NativeLibraryLoader
{
    private static bool _loaded;

    internal static void EnsureLoaded()
    {
        if (_loaded)
            return;
        _loaded = true;

        string? path = Environment.GetEnvironmentVariable("ZLINK_LIBRARY_PATH");
        if (!string.IsNullOrWhiteSpace(path))
        {
            NativeLibrary.Load(path);
            return;
        }

        try
        {
            NativeLibrary.Load("zlink");
            return;
        }
        catch
        {
            // fall through to bundled load
        }

        string? baseDir = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
        if (string.IsNullOrEmpty(baseDir))
            return;

        string rid = GetRid();
        string libName = GetLibName();
        string candidate = Path.Combine(baseDir, "runtimes", rid, "native", libName);
        if (File.Exists(candidate))
        {
            NativeLibrary.Load(candidate);
        }
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

    private static string GetLibName()
    {
        if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
            return "zlink.dll";
        if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
            return "libzlink.dylib";
        return "libzlink.so";
    }
}
