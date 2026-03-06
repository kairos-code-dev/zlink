using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Threading;
using Zlink;
using TcpListener = System.Net.Sockets.TcpListener;

internal static partial class PerfRunner
{
    internal delegate bool PayloadHandler(ReadOnlySpan<byte> payload);

    internal const uint PerfMetricMagic = 0x4D50_4631u; // "MPF1"
    internal const int PerfMetricHeaderSize = 32;
    internal const int ErrnoEintr = 4;
    internal const int ErrnoEagain = 11;
    private static readonly object IpcLock = new();
    private static readonly HashSet<string> IpcPaths = new();
    private static bool IpcCleanupHooked;

    internal enum PerfPhase : uint
    {
        Unknown = 0,
        Warmup = 1,
        Active = 2,
        Drain = 3,
    }

    internal readonly struct PerfMetricHeader
    {
        internal readonly uint Magic;
        internal readonly uint RunId;
        internal readonly uint Phase;
        internal readonly uint MsgSize;
        internal readonly ulong Seq;
        internal readonly ulong SentTsUs;

        internal PerfMetricHeader(uint magic, uint runId, uint phase,
            uint msgSize, ulong seq, ulong sentTsUs)
        {
            Magic = magic;
            RunId = runId;
            Phase = phase;
            MsgSize = msgSize;
            Seq = seq;
            SentTsUs = sentTsUs;
        }
    }

    internal static int ReceiveBlocking(Zlink.Socket socket, Span<byte> buffer,
        ReceiveFlags flags = ReceiveFlags.None)
    {
        while (true)
        {
            try
            {
                return socket.Receive(buffer, flags);
            }
            catch (ZlinkException ex) when (IsInterrupted(ex.Errno))
            {
                continue;
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.Errno))
            {
                if ((flags & ReceiveFlags.DontWait) != 0)
                    return 0;
                throw;
            }
        }
    }

    internal static int TryReceiveNonBlocking(Zlink.Socket socket,
        Span<byte> buffer)
    {
        return ReceiveBlocking(socket, buffer, ReceiveFlags.DontWait);
    }

    internal static int ReceiveRetry(Zlink.Socket socket, Span<byte> buffer,
        ReceiveFlags flags = ReceiveFlags.None)
    {
        while (true)
        {
            try
            {
                return socket.Receive(buffer, flags);
            }
            catch (ZlinkException ex) when (IsInterrupted(ex.Errno))
            {
                continue;
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.Errno))
            {
                return 0;
            }
        }
    }

    internal static int DrainReadableSocket(Zlink.Socket socket, Span<byte> buffer,
        PayloadHandler onMessage)
    {
        int count = 0;
        while (true)
        {
            int n = TryReceiveNonBlocking(socket, buffer);
            if (n <= 0)
                break;

            count++;
            if (!onMessage(buffer.Slice(0, n)))
                break;
        }

        return count;
    }

    internal static bool WaitForEvents(Poller poller, List<PollEvent> events,
        int timeoutMs)
    {
        events.Clear();
        return poller.Wait(events, timeoutMs) > 0;
    }

    internal static int SendBlocking(Zlink.Socket socket, ReadOnlySpan<byte> buffer,
        SendFlags flags = SendFlags.None)
    {
        return socket.Send(buffer, flags);
    }

    internal static int ParsePositiveEnv(string name, int defaultValue)
    {
        var raw = Environment.GetEnvironmentVariable(name);
        if (int.TryParse(raw, out int parsed) && parsed > 0)
            return parsed;
        return defaultValue;
    }

    internal static int ParseNonNegativeEnv(string name, int defaultValue)
    {
        var raw = Environment.GetEnvironmentVariable(name);
        if (int.TryParse(raw, out int parsed) && parsed >= 0)
            return parsed;
        return defaultValue;
    }

    internal static bool WaitUntil(Func<bool> check, int timeoutMs,
        int intervalMs = 10)
    {
        long deadlineTicks = Stopwatch.GetTimestamp()
            + (long)Math.Max(1, timeoutMs) * Stopwatch.Frequency / 1000;
        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            try
            {
                if (check())
                    return true;
            }
            catch
            {
            }

            if (intervalMs > 0)
                Thread.Sleep(intervalMs);
            else
                Thread.Yield();
        }

        return false;
    }

    internal static bool IsEchoPattern(string pattern)
    {
        string normalized = NormalizePerfPattern(pattern);
        return normalized == "DEALER_ROUTER"
            || normalized == "ROUTER_ROUTER"
            || normalized == "GATEWAY"
            || normalized == "STREAM"
            || normalized == "STREAM_CALLBACK"
            || normalized == "STREAM_LEN32BE";
    }

    internal static void PrintResult(string pattern, string transport, int size,
        double throughput, double latencyUs)
    {
        PrintResult(pattern, transport, size, throughput, latencyUs, latencyUs,
            latencyUs);
    }

    internal static void PrintResult(string pattern, string transport, int size,
        double throughput, double latencyUs, double latencyP95Us,
        double latencyP99Us)
    {
        double bandwidth = BandwidthMbps(pattern, throughput, size);
        double latencyMs = UsToMs(latencyUs);
        double latencyP95Ms = UsToMs(latencyP95Us);
        double latencyP99Ms = UsToMs(latencyP99Us);
        Console.WriteLine(
            $"RESULT,current,{pattern},{transport},{size},throughput,{throughput}");
        Console.WriteLine(
            $"RESULT,current,{pattern},{transport},{size},bandwidth,{bandwidth}");
        Console.WriteLine(
            $"RESULT,current,{pattern},{transport},{size},latency,{latencyMs}");
        Console.WriteLine(
            $"RESULT,current,{pattern},{transport},{size},latency_p95,{latencyP95Ms}");
        Console.WriteLine(
            $"RESULT,current,{pattern},{transport},{size},latency_p99,{latencyP99Ms}");
    }

    private static double BandwidthMbps(string pattern, double throughput, int size)
    {
        double multiplier = IsEchoPattern(pattern) ? 2.0 : 1.0;
        return (throughput * size * multiplier) / 1_000_000.0;
    }

    private static double UsToMs(double latencyUs)
    {
        return latencyUs / 1000.0;
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

    internal static void ReservoirSample(List<double> samples, double value,
        ref long seenCount, int cap, ref uint rngState)
    {
        if (cap <= 0)
            return;

        if (samples.Count < cap)
        {
            samples.Add(value);
            seenCount++;
            return;
        }

        seenCount++;
        uint r = NextRandom(ref rngState);
        long slot = r % seenCount;
        if (slot < samples.Count)
            samples[(int)slot] = value;
    }

    internal static (double mean, double p95, double p99) ComputeLatencyStats(
        List<double> samples)
    {
        if (samples.Count == 0)
            return (0.0, 0.0, 0.0);

        double sum = 0.0;
        for (int i = 0; i < samples.Count; i++)
            sum += samples[i];

        samples.Sort();
        int p95Index = Math.Min(samples.Count - 1,
            (int)Math.Ceiling(samples.Count * 0.95) - 1);
        int p99Index = Math.Min(samples.Count - 1,
            (int)Math.Ceiling(samples.Count * 0.99) - 1);
        return (sum / samples.Count, samples[p95Index], samples[p99Index]);
    }

    internal static long TimestampUs()
    {
        long ts = Stopwatch.GetTimestamp();
        return (long)(ts * (1_000_000.0 / Stopwatch.Frequency));
    }

    internal static ulong EpochUs()
    {
        return (ulong)(DateTime.UtcNow.Ticks / 10L);
    }

    internal static bool StampMetricHeader(Span<byte> payload, uint runId,
        PerfPhase phase, int msgSize, ulong seq, ulong sentTsUs)
    {
        if (payload.Length < PerfMetricHeaderSize)
            return false;

        BinaryPrimitives.WriteUInt32LittleEndian(payload.Slice(0, 4),
            PerfMetricMagic);
        BinaryPrimitives.WriteUInt32LittleEndian(payload.Slice(4, 4), runId);
        BinaryPrimitives.WriteUInt32LittleEndian(payload.Slice(8, 4),
            (uint)phase);
        BinaryPrimitives.WriteUInt32LittleEndian(payload.Slice(12, 4),
            (uint)Math.Max(0, msgSize));
        BinaryPrimitives.WriteUInt64LittleEndian(payload.Slice(16, 8), seq);
        BinaryPrimitives.WriteUInt64LittleEndian(payload.Slice(24, 8),
            sentTsUs);
        return true;
    }

    internal static bool TryDecodeMetricHeader(ReadOnlySpan<byte> payload,
        out PerfMetricHeader header)
    {
        header = default;
        if (payload.Length < PerfMetricHeaderSize)
            return false;

        uint magic = BinaryPrimitives.ReadUInt32LittleEndian(payload.Slice(0,
            4));
        uint runId = BinaryPrimitives.ReadUInt32LittleEndian(payload.Slice(4,
            4));
        uint phase = BinaryPrimitives.ReadUInt32LittleEndian(payload.Slice(8,
            4));
        uint msgSize = BinaryPrimitives.ReadUInt32LittleEndian(payload.Slice(12,
            4));
        ulong seq = BinaryPrimitives.ReadUInt64LittleEndian(payload.Slice(16,
            8));
        ulong sentTsUs = BinaryPrimitives.ReadUInt64LittleEndian(payload.Slice(
            24, 8));

        header = new PerfMetricHeader(magic, runId, phase, msgSize, seq,
            sentTsUs);
        return magic == PerfMetricMagic;
    }

    private static uint NextRandom(ref uint state)
    {
        if (state == 0)
            state = 0xA341316Cu;
        uint x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;
        return x;
    }

    internal static bool IsWouldBlock(int errno)
    {
        ErrorCode code = ZlinkException.MapErrorCode(errno);
        return code == ErrorCode.EAgain || errno == ErrnoEagain;
    }

    internal static bool IsInterrupted(int errno)
    {
        ErrorCode code = ZlinkException.MapErrorCode(errno);
        return code == ErrorCode.EIntr || errno == ErrnoEintr;
    }

    internal static bool IsTransientNetworkError(int errno)
    {
        ErrorCode code = ZlinkException.MapErrorCode(errno);
        return code == ErrorCode.EHostUnreach
               || code == ErrorCode.ENetUnreach
               || code == ErrorCode.ENotConn
               || code == ErrorCode.EConnRefused;
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
