using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Net;
using System.Threading;
using Zlink;
using Zlink.Service;
using TcpListener = System.Net.Sockets.TcpListener;

public static class PerfShared
{
    public const int ErrnoEintr = 4;
    public const int ErrnoEagain = 11;
    public const uint PerfMetricMagic = 0x4D50_4631u;
    public const int PerfMetricHeaderSize = 32;

    public static long TimestampUs()
    {
        long ts = Stopwatch.GetTimestamp();
        return (long)(ts * (1_000_000.0 / Stopwatch.Frequency));
    }

    public static ulong EpochUs()
    {
        return (ulong)(DateTime.UtcNow.Ticks / 10L);
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
            string path = $"/tmp/zlink-bench-{name}-{GetPort()}.sock";
            TryDeleteFile(path);
            AppDomain.CurrentDomain.ProcessExit += (_, _) => TryDeleteFile(path);
            return $"ipc://{path}";
        }

        return $"{transport}://127.0.0.1:{GetPort()}";
    }

    public static void PrintResult(string pattern, string transport, int size,
        double throughput, double latencyUs, double latencyP95Us,
        double latencyP99Us, double bandwidthMultiplier, bool fixedFormat)
    {
        double bandwidth = (throughput * size * bandwidthMultiplier) / 1_000_000.0;
        WriteMetric("throughput", throughput);
        WriteMetric("bandwidth", bandwidth);
        WriteMetric("latency", latencyUs / 1000.0);
        WriteMetric("latency_p95", latencyP95Us / 1000.0);
        WriteMetric("latency_p99", latencyP99Us / 1000.0);
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
        Console.WriteLine($"UNSUPPORTED,{pattern},{transport},{size},{reason}");
        return 0;
    }

    public static void StampHeader(Span<byte> header, long tsUs)
    {
        if (header.Length < 8)
            throw new ArgumentException("header length must be >= 8", nameof(header));

        BinaryPrimitives.WriteInt64LittleEndian(header, tsUs);
    }

    public static long DecodeHeader(ReadOnlySpan<byte> header)
    {
        if (header.Length < 8)
            throw new ArgumentException("header length must be >= 8", nameof(header));

        return BinaryPrimitives.ReadInt64LittleEndian(header);
    }

    public static bool StampMetricHeader(Span<byte> payload, uint runId,
        uint phase, int msgSize, ulong seq, ulong sentTsUs)
    {
        if (payload.Length < PerfMetricHeaderSize)
            return false;

        BinaryPrimitives.WriteUInt32LittleEndian(payload.Slice(0, 4),
            PerfMetricMagic);
        BinaryPrimitives.WriteUInt32LittleEndian(payload.Slice(4, 4), runId);
        BinaryPrimitives.WriteUInt32LittleEndian(payload.Slice(8, 4), phase);
        BinaryPrimitives.WriteUInt32LittleEndian(payload.Slice(12, 4),
            (uint)Math.Max(0, msgSize));
        BinaryPrimitives.WriteUInt64LittleEndian(payload.Slice(16, 8), seq);
        BinaryPrimitives.WriteUInt64LittleEndian(payload.Slice(24, 8),
            sentTsUs);
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
        uint phase = BinaryPrimitives.ReadUInt32LittleEndian(payload.Slice(8, 4));
        uint msgSize = BinaryPrimitives.ReadUInt32LittleEndian(payload.Slice(12, 4));
        ulong seq = BinaryPrimitives.ReadUInt64LittleEndian(payload.Slice(16, 8));
        ulong sentTsUs = BinaryPrimitives.ReadUInt64LittleEndian(
            payload.Slice(24, 8));

        header = new PerfMetricHeader(magic, runId, phase, msgSize, seq,
            sentTsUs);
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

    private static int GetPort()
    {
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        int port = ((IPEndPoint)listener.LocalEndpoint).Port;
        listener.Stop();
        return port;
    }
}

public readonly struct PerfMetricHeader
{
    public PerfMetricHeader(uint magic, uint runId, uint phase, uint msgSize,
        ulong seq, ulong sentTsUs)
    {
        Magic = magic;
        RunId = runId;
        Phase = phase;
        MsgSize = msgSize;
        Seq = seq;
        SentTsUs = sentTsUs;
    }

    public uint Magic { get; }
    public uint RunId { get; }
    public uint Phase { get; }
    public uint MsgSize { get; }
    public ulong Seq { get; }
    public ulong SentTsUs { get; }
}
