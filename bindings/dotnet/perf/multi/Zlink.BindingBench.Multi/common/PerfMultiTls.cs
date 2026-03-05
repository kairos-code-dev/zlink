using System;
using System.IO;
using Zlink;

internal static partial class PerfRunner
{
    private static void ConfigureTlsServerIfNeeded(Zlink.Socket socket,
        string transport)
    {
        if (!transport.Equals("tls", StringComparison.OrdinalIgnoreCase)
            && !transport.Equals("wss", StringComparison.OrdinalIgnoreCase))
        {
            return;
        }

        if (!TryResolvePerfTlsPaths(out string certPath, out string keyPath,
                out _))
        {
            throw new InvalidOperationException(
                "TLS certificate files not found under bindings/dotnet/tests/certs");
        }

        socket.SetOption(SocketOptions.TlsCert, certPath);
        socket.SetOption(SocketOptions.TlsKey, keyPath);
    }

    private static void ConfigureTlsClientIfNeeded(Zlink.Socket socket,
        string transport)
    {
        if (!transport.Equals("tls", StringComparison.OrdinalIgnoreCase)
            && !transport.Equals("wss", StringComparison.OrdinalIgnoreCase))
        {
            return;
        }

        if (!TryResolvePerfTlsPaths(out _, out _, out string caPath))
        {
            throw new InvalidOperationException(
                "TLS CA file not found under bindings/dotnet/tests/certs");
        }

        socket.SetOption(SocketOptions.TlsCa, caPath);
    }

    private static bool TryResolvePerfTlsPaths(out string certPath,
        out string keyPath, out string caPath)
    {
        certPath = string.Empty;
        keyPath = string.Empty;
        caPath = string.Empty;

        string[] roots =
        {
            AppContext.BaseDirectory,
            Directory.GetCurrentDirectory(),
        };

        foreach (string start in roots)
        {
            var dir = new DirectoryInfo(start);
            while (dir != null)
            {
                string[] certDirs =
                {
                    Path.Combine(dir.FullName, "bindings", "dotnet", "tests",
                        "certs"),
                    Path.Combine(dir.FullName, "tests", "certs"),
                };
                foreach (string certDir in certDirs)
                {
                    string cert = Path.Combine(certDir, "server.crt");
                    string key = Path.Combine(certDir, "server.key");
                    string ca = Path.Combine(certDir, "ca.crt");
                    if (File.Exists(cert) && File.Exists(key) && File.Exists(ca))
                    {
                        certPath = cert;
                        keyPath = key;
                        caPath = ca;
                        return true;
                    }
                }
                dir = dir.Parent;
            }
        }

        return false;
    }
}
