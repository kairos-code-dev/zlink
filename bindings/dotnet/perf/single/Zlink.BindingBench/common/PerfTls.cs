using System;
using System.IO;
using Zlink;

internal static partial class PerfRunner
{
    internal static void ConfigureTlsServerIfNeeded(Zlink.Socket socket,
        string transport)
    {
        if (!IsSecureTransport(transport))
            return;

        if (!TryResolvePerfTlsPaths(out string certPath, out string keyPath,
                out _))
        {
            throw new InvalidOperationException(
                "TLS certificate files not found under bindings/dotnet/tests/certs");
        }

        socket.SetOption(SocketOptions.TlsCert, certPath);
        socket.SetOption(SocketOptions.TlsKey, keyPath);
    }

    internal static void ConfigureTlsClientIfNeeded(Zlink.Socket socket,
        string transport)
    {
        if (!IsSecureTransport(transport))
            return;

        if (!TryResolvePerfTlsPaths(out _, out _, out string caPath))
        {
            throw new InvalidOperationException(
                "TLS CA file not found under bindings/dotnet/tests/certs");
        }

        socket.SetOption(SocketOptions.TlsCa, caPath);
        socket.SetOption(SocketOptions.TlsTrustSystem, 0);
        socket.SetOption(SocketOptions.TlsHostname, "localhost");
    }

    internal static void ConfigureReceiverTlsServerIfNeeded(Receiver receiver,
        string transport)
    {
        if (!IsSecureTransport(transport))
            return;

        if (!TryResolvePerfTlsPaths(out string certPath, out string keyPath,
                out _))
        {
            throw new InvalidOperationException(
                "TLS certificate files not found under bindings/dotnet/tests/certs");
        }

        receiver.SetTlsServer(certPath, keyPath);
    }

    internal static void ConfigureSpotTlsPublisherIfNeeded(SpotNode spotNode,
        string transport)
    {
        if (!IsSecureTransport(transport))
            return;

        if (!TryResolvePerfTlsPaths(out string certPath, out string keyPath,
                out _))
        {
            throw new InvalidOperationException(
                "TLS certificate files not found under bindings/dotnet/tests/certs");
        }

        spotNode.SetTlsServer(certPath, keyPath);
    }

    internal static void ConfigureSpotTlsSubscriberIfNeeded(SpotNode spotNode,
        string transport)
    {
        if (!IsSecureTransport(transport))
            return;

        if (!TryResolvePerfTlsPaths(out _, out _, out string caPath))
        {
            throw new InvalidOperationException(
                "TLS CA file not found under bindings/dotnet/tests/certs");
        }

        spotNode.SetTlsClient(caPath, "localhost", false);
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
                string certDir = Path.Combine(dir.FullName, "bindings", "dotnet",
                    "tests", "certs");
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

                dir = dir.Parent;
            }
        }

        return false;
    }
}
