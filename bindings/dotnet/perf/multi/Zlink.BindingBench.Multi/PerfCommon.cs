using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Threading;
using Zlink;
using TcpListener = System.Net.Sockets.TcpListener;

internal static partial class PerfRunner
{
    internal const int ErrnoEintr = 4;
    internal const int ErrnoEagain = 11;
    private static readonly object IpcLock = new();
    private static readonly HashSet<string> IpcPaths = new();
    private static bool IpcCleanupHooked;

    internal static int ReceiveRetry(Zlink.Socket socket, Span<byte> buffer,
        ReceiveFlags flags = ReceiveFlags.None)
    {
        ReceiveFlags operationFlags = flags | ReceiveFlags.DontWait;
        while (true)
        {
            try
            {
                return socket.Receive(buffer, operationFlags);
            }
            catch (ZlinkException ex) when (IsInterrupted(ex.Errno))
            {
                continue;
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.Errno))
            {
                Thread.Sleep(1);
                continue;
            }
        }
    }

    internal static int SendRetry(Zlink.Socket socket, ReadOnlySpan<byte> buffer,
        SendFlags flags = SendFlags.None)
    {
        SendFlags operationFlags = flags | SendFlags.DontWait;
        while (true)
        {
            try
            {
                return socket.Send(buffer, operationFlags);
            }
            catch (ZlinkException ex) when (IsInterrupted(ex.Errno))
            {
                continue;
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.Errno))
            {
                Thread.Sleep(1);
                continue;
            }
        }
    }

    internal static int ParsePositiveEnv(string name, int defaultValue)
    {
        var raw = Environment.GetEnvironmentVariable(name);
        if (int.TryParse(raw, out int parsed) && parsed > 0)
            return parsed;
        return defaultValue;
    }

    private static bool IsWouldBlock(int errno)
    {
        ErrorCode code = ZlinkException.MapErrorCode(errno);
        return code == ErrorCode.EAgain || errno == ErrnoEagain;
    }

    private static bool IsInterrupted(int errno)
    {
        ErrorCode code = ZlinkException.MapErrorCode(errno);
        return code == ErrorCode.EIntr || errno == ErrnoEintr;
    }

    internal static int ParseNonNegativeEnv(string name, int defaultValue)
    {
        var raw = Environment.GetEnvironmentVariable(name);
        if (int.TryParse(raw, out int parsed) && parsed >= 0)
            return parsed;
        return defaultValue;
    }

    internal static bool IsEchoPattern(string pattern)
    {
        return pattern.Equals("MULTI_DEALER_ROUTER",
                StringComparison.OrdinalIgnoreCase)
            || pattern.Equals("MULTI_ROUTER_ROUTER",
                StringComparison.OrdinalIgnoreCase)
            || pattern.Equals("MULTI_STREAM", StringComparison.OrdinalIgnoreCase)
            || pattern.Equals("MULTI_STREAM_CALLBACK",
                StringComparison.OrdinalIgnoreCase)
            || pattern.Equals("MULTI_STREAM_LEN32BE",
                StringComparison.OrdinalIgnoreCase);
    }

    internal static bool IsOneWayPattern(string pattern)
    {
        return pattern.Equals("MULTI_DEALER_DEALER",
                StringComparison.OrdinalIgnoreCase)
            || pattern.Equals("MULTI_PUBSUB", StringComparison.OrdinalIgnoreCase)
            || pattern.Equals("MULTI_GATEWAY", StringComparison.OrdinalIgnoreCase)
            || pattern.Equals("MULTI_SPOT", StringComparison.OrdinalIgnoreCase);
    }

    internal static void PrintResult(string pattern, string transport, int size,
        double throughput, double latencyUs)
    {
        double bandwidth = BandwidthMbps(pattern, throughput, size);
        Console.WriteLine(
            $"RESULT,current,{pattern},{transport},{size},throughput,{throughput}");
        Console.WriteLine(
            $"RESULT,current,{pattern},{transport},{size},bandwidth,{bandwidth}");
        Console.WriteLine(
            $"RESULT,current,{pattern},{transport},{size},latency,{latencyUs}");
    }

    private static double BandwidthMbps(string pattern, double throughput, int size)
    {
        double multiplier = IsEchoPattern(pattern) ? 2.0 : 1.0;
        return (throughput * size * multiplier) / 1_000_000.0;
    }

    internal static string EndpointFor(string transport, string name)
    {
        if (transport == "inproc")
            return $"inproc://bench-{name}-{Guid.NewGuid()}";
        if (transport == "ipc")
        {
            string endpoint = $"ipc:///tmp/zlink-bench-{name}-{GetPort()}.sock";
            RegisterIpcEndpoint(endpoint);
            return endpoint;
        }
        return $"{transport}://127.0.0.1:{GetPort()}";
    }

    private static void RegisterIpcEndpoint(string endpoint)
    {
        const string prefix = "ipc://";
        if (!endpoint.StartsWith(prefix, StringComparison.Ordinal))
            return;

        string path = endpoint.Substring(prefix.Length);
        if (path.Length == 0 || path[0] != '/')
            return;

        lock (IpcLock)
        {
            IpcPaths.Add(path);
            TryDeleteFile(path);
            if (!IpcCleanupHooked)
            {
                AppDomain.CurrentDomain.ProcessExit += (_, _) => CleanupIpcFiles();
                IpcCleanupHooked = true;
            }
        }
    }

    private static void CleanupIpcFiles()
    {
        List<string> snapshot;
        lock (IpcLock)
            snapshot = new List<string>(IpcPaths);

        foreach (string path in snapshot)
            TryDeleteFile(path);
    }

    private static void TryDeleteFile(string path)
    {
        try
        {
            if (File.Exists(path))
                File.Delete(path);
        }
        catch
        {
            // Best effort cleanup only.
        }
    }

    private static int GetPort()
    {
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        int port = ((IPEndPoint)listener.LocalEndpoint).Port;
        listener.Stop();
        return port;
    }
}
