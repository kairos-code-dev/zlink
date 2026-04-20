using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Threading;
using System.Net.Sockets;
using Zlink;

public static class PerfShared
{
    public const int ErrnoEintr = 4;
    public const int ErrnoEagain = 11;
    public const int ErrnoEperm = 1;
    public const int ErrnoEaccess = 13;
    public const uint PerfMetricMagic = 0x5A4C_4E4Bu;
    public const int PerfMetricHeaderSize = 29;
    private static int _nextPort = InitializePortSeed();

    public static long TimestampNs()
    {
        long ts = Stopwatch.GetTimestamp();
        return (long)(ts * (1_000_000_000.0 / Stopwatch.Frequency));
    }

    public static ulong EpochNs()
    {
        return (ulong)(DateTime.UtcNow.Ticks * 100L);
    }

    public static long DeadlineTicksFromMilliseconds(int milliseconds)
    {
        long boundedMs = Math.Max(1, milliseconds);
        long deltaTicks = (boundedMs * Stopwatch.Frequency) / 1000;
        if (deltaTicks <= 0)
            deltaTicks = 1;
        return Stopwatch.GetTimestamp() + deltaTicks;
    }

    public static long DeadlineTicksFromSeconds(int seconds)
    {
        int boundedSeconds = Math.Max(1, seconds);
        return Stopwatch.GetTimestamp()
            + ((long)boundedSeconds * Stopwatch.Frequency);
    }

    public static double ElapsedSecondsFromTicks(long startTicks, long endTicks)
    {
        long deltaTicks = Math.Max(0, endTicks - startTicks);
        return deltaTicks / (double)Stopwatch.Frequency;
    }

    public static bool WaitSpotPeerConnected(SpotNode node, int timeoutMs)
    {
        long deadlineTicks = DeadlineTicksFromMilliseconds(timeoutMs);
        var spin = new SpinWait();
        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            if (node.StatusSnapshot().ConnectedPeerCount > 0)
                return true;
            spin.SpinOnce();
        }

        return node.StatusSnapshot().ConnectedPeerCount > 0;
    }

    public static void ReservoirSample(List<double> samples, double value,
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

    public static (double mean, double p95, double p99) ComputeLatencyStats(
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

    public static string EndpointFor(string transport, string name)
    {
        if (transport == "inproc")
            return $"inproc://bench-{name}-{Guid.NewGuid()}";

        if (transport == "ipc")
        {
            string path = $"/tmp/zlink-bench-{name}-{Guid.NewGuid():N}.sock";
            TryDeleteFile(path);
            AppDomain.CurrentDomain.ProcessExit += (_, _) => TryDeleteFile(path);
            return $"ipc://{path}";
        }

        return $"{transport}://127.0.0.1:{GetPort()}";
    }

    private static double NsToMs(double latencyNs)
    {
        return latencyNs / 1_000_000.0;
    }

    public static void PrintResult(string pattern, string transport, int size,
        double throughput, double latencyNs, double latencyP95Ns,
        double latencyP99Ns, double bandwidthMultiplier, bool fixedFormat)
    {
        double bandwidth = (throughput * size * bandwidthMultiplier) / 1_000_000.0;
        WriteMetric("throughput", throughput);
        WriteMetric("bandwidth", bandwidth);
        WriteMetric("latency", NsToMs(latencyNs));
        WriteMetric("latency_p95", NsToMs(latencyP95Ns));
        WriteMetric("latency_p99", NsToMs(latencyP99Ns));
        return;

        void WriteMetric(string metric, double value)
        {
            string formatted = fixedFormat
                ? value.ToString("F3", CultureInfo.InvariantCulture)
                : value.ToString(CultureInfo.InvariantCulture);
            Console.WriteLine(
                $"RESULT,current,{pattern},{transport},{size},{metric},{formatted}");
        }
    }

    public static int PrintUnsupported(string pattern, string transport,
        int size, string reason)
    {
        _ = size;
        _ = reason;
        Console.WriteLine($"UNSUPPORTED,current,{pattern},{transport}");
        return 0;
    }

    public static bool TryPrintUnsupportedTransportFailure(string pattern,
        string transport, int size, Exception ex)
    {
        if (!IsSandboxRestrictedTransport(transport))
            return false;

        if (IsSandboxRestrictedNetworkException(ex))
        {
            PrintUnsupported(pattern, transport, size,
                "sandbox_network_restricted");
            return true;
        }

        if (!TryResolvePermissionErrno(ex, out int errno)
            || !IsPermissionRestrictedErrno(errno))
        {
            return false;
        }

        PrintUnsupported(pattern, transport, size,
            $"sandbox_network_restricted_errno_{errno}");
        return true;
    }

    public static void StampHeader(Span<byte> header, long tsNs)
    {
        if (header.Length < 8)
            throw new ArgumentException("header length must be >= 8", nameof(header));

        BinaryPrimitives.WriteInt64LittleEndian(header, tsNs);
    }

    public static long DecodeHeader(ReadOnlySpan<byte> header)
    {
        if (header.Length < 8)
            throw new ArgumentException("header length must be >= 8", nameof(header));

        return BinaryPrimitives.ReadInt64LittleEndian(header);
    }

    public static bool StampMetricHeader(Span<byte> payload, uint runId,
        uint phase, int msgSize, ulong seq, ulong sentTsNs)
    {
        if (payload.Length < PerfMetricHeaderSize)
            return false;

        BinaryPrimitives.WriteUInt32LittleEndian(payload.Slice(0, 4),
            PerfMetricMagic);
        BinaryPrimitives.WriteUInt32LittleEndian(payload.Slice(4, 4), runId);
        payload[8] = checked((byte)phase);
        BinaryPrimitives.WriteUInt32LittleEndian(payload.Slice(9, 4),
            (uint)Math.Max(0, msgSize));
        BinaryPrimitives.WriteUInt64LittleEndian(payload.Slice(13, 8), seq);
        BinaryPrimitives.WriteInt64LittleEndian(payload.Slice(21, 8),
            checked((long)sentTsNs));
        return true;
    }

    public static bool TryDecodeMetricHeader(ReadOnlySpan<byte> payload,
        out PerfMetricHeader header)
    {
        header = default;
        if (payload.Length < PerfMetricHeaderSize)
            return false;

        uint magic = BinaryPrimitives.ReadUInt32LittleEndian(payload.Slice(0, 4));
        uint runId = BinaryPrimitives.ReadUInt32LittleEndian(payload.Slice(4, 4));
        uint phase = payload[8];
        uint msgSize = BinaryPrimitives.ReadUInt32LittleEndian(payload.Slice(9, 4));
        ulong seq = BinaryPrimitives.ReadUInt64LittleEndian(payload.Slice(13, 8));
        ulong sentTsNs = checked((ulong)BinaryPrimitives.ReadInt64LittleEndian(
            payload.Slice(21, 8)));

        header = new PerfMetricHeader(magic, runId, phase, msgSize, seq,
            sentTsNs);
        return magic == PerfMetricMagic;
    }

    public static bool IsWouldBlock(int errno)
    {
        ErrorCode code = ZlinkException.MapErrorCode(errno);
        return code == ErrorCode.EAgain || errno == ErrnoEagain;
    }

    public static bool IsInterrupted(int errno)
    {
        ErrorCode code = ZlinkException.MapErrorCode(errno);
        return code == ErrorCode.EIntr || errno == ErrnoEintr;
    }

    public static bool IsSandboxRestrictedTransport(string transport)
    {
        return string.Equals(transport, "tcp", StringComparison.OrdinalIgnoreCase)
               || string.Equals(transport, "tls", StringComparison.OrdinalIgnoreCase)
               || string.Equals(transport, "ws", StringComparison.OrdinalIgnoreCase)
               || string.Equals(transport, "wss", StringComparison.OrdinalIgnoreCase)
               || string.Equals(transport, "ipc", StringComparison.OrdinalIgnoreCase);
    }

    public static void TryDisposeQuietly(IDisposable? disposable)
    {
        if (disposable == null)
            return;

        try
        {
            disposable.Dispose();
        }
        catch
        {
        }
    }

    public static void TryDisposeAllQuietly(params IDisposable?[] disposables)
    {
        if (disposables == null)
            return;

        foreach (IDisposable? disposable in disposables)
            TryDisposeQuietly(disposable);
    }

    public static string NormalizePattern(string pattern,
        bool trimMultiPrefix = false)
    {
        string normalized = (pattern ?? string.Empty).Trim().ToUpperInvariant();
        const string multiPrefix = "MULTI_";
        if (trimMultiPrefix
            && normalized.StartsWith(multiPrefix, StringComparison.Ordinal))
        {
            return normalized.Substring(multiPrefix.Length);
        }

        return normalized;
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

    private static int InitializePortSeed()
    {
        int pid = Environment.ProcessId;
        int tick = Environment.TickCount & 0x7FFFFFFF;
        return 20000 + Math.Abs(((pid * 131) ^ tick) % 20000);
    }

    private static int GetPort()
    {
        while (true)
        {
            int next = Interlocked.Increment(ref _nextPort);
            if (next <= 59999)
                return next;

            int reset = InitializePortSeed();
            Interlocked.CompareExchange(ref _nextPort, reset, next);
        }
    }

    private static bool IsPermissionRestrictedErrno(int errno)
    {
        ErrorCode code = ZlinkException.MapErrorCode(errno);
        return errno == ErrnoEperm
               || errno == ErrnoEaccess
               || code == ErrorCode.EAccess;
    }

    private static bool IsSandboxRestrictedNetworkException(Exception ex)
    {
        if (ex is SocketException
            || ex is ZlinkBindException
            || ex is ZlinkConnectException
            || ex is ZlinkException)
        {
            return true;
        }

        return ex.InnerException != null
               && IsSandboxRestrictedNetworkException(ex.InnerException);
    }

    private static bool TryResolvePermissionErrno(Exception ex, out int errno)
    {
        switch (ex)
        {
            case ZlinkException zlinkEx when zlinkEx.InternalErrno != 0:
                errno = zlinkEx.InternalErrno;
                return true;
            case SocketException socketEx when socketEx.NativeErrorCode != 0:
                errno = socketEx.NativeErrorCode;
                return true;
        }

        if (ex.InnerException != null
            && TryResolvePermissionErrno(ex.InnerException, out errno))
        {
            return true;
        }

        string text = ex.ToString();
        if (text.IndexOf("errno 1", StringComparison.OrdinalIgnoreCase) >= 0
            || text.IndexOf("operation not permitted",
                StringComparison.OrdinalIgnoreCase) >= 0)
        {
            errno = ErrnoEperm;
            return true;
        }

        if (text.IndexOf("errno 13", StringComparison.OrdinalIgnoreCase) >= 0
            || text.IndexOf("permission denied",
                StringComparison.OrdinalIgnoreCase) >= 0)
        {
            errno = ErrnoEaccess;
            return true;
        }

        errno = 0;
        return false;
    }
}

public readonly struct PerfMetricHeader
{
    public PerfMetricHeader(uint magic, uint runId, uint phase, uint msgSize,
        ulong seq, ulong sentTsNs)
    {
        Magic = magic;
        RunId = runId;
        Phase = phase;
        MsgSize = msgSize;
        Seq = seq;
        SentTsNs = sentTsNs;
    }

    public uint Magic { get; }
    public uint RunId { get; }
    public uint Phase { get; }
    public uint MsgSize { get; }
    public ulong Seq { get; }
    public ulong SentTsNs { get; }
}
