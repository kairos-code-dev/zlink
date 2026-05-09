using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Threading;
using Systems.Zlink;

public static class PerfShared
{
    public const int ErrnoEintr = 4;
    public const int ErrnoEagain = 11;
    public const int ErrnoEintrWin = 10004;
    public const int ErrnoEagainAlt = 35;
    public const int ErrnoEagainWin = 10035;
    public const uint PerfMetricMagic = 0x5A4C_4E4Bu;
    public const int PerfMetricHeaderSize = 29;
    private static int _nextPort = InitializePortSeed();
    private static readonly long EpochBaseTimestamp = Stopwatch.GetTimestamp();
    private static readonly ulong EpochBaseNs = (ulong)(DateTime.UtcNow.Ticks * 100L);
    private static readonly double StopwatchTickNs =
        1_000_000_000.0 / Stopwatch.Frequency;

    public static long TimestampNs()
    {
        long ts = Stopwatch.GetTimestamp();
        return (long)(ts * (1_000_000_000.0 / Stopwatch.Frequency));
    }

    public static ulong EpochNs()
    {
        return EpochNsFromTimestamp(Stopwatch.GetTimestamp());
    }

    public static ulong EpochNsFromTimestamp(long timestamp)
    {
        long deltaTicks = timestamp - EpochBaseTimestamp;
        ulong deltaNs = (ulong)(deltaTicks * StopwatchTickNs);
        return EpochBaseNs + deltaNs;
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
            WriteStdoutLine(
                $"RESULT,dotnet,{pattern},{transport},{size},{metric},{formatted}");
        }
    }

    public static int PrintUnsupported(string pattern, string transport,
        int size, string reason)
    {
        _ = size;
        _ = reason;
        WriteStdoutLine($"UNSUPPORTED,dotnet,{pattern},{transport}");
        return 0;
    }

    public static void WriteStdoutLine(string line)
    {
        Console.WriteLine(line);
        Console.Out.Flush();
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
        return errno == ErrnoEagain
            || errno == ErrnoEagainAlt
            || errno == ErrnoEagainWin;
    }

    public static bool IsInterrupted(int errno)
    {
        return errno == ErrnoEintr || errno == ErrnoEintrWin;
    }

    public static bool IsTransientNetworkError(int errno)
    {
        return errno == 51
            || errno == 57
            || errno == 61
            || errno == 65
            || errno == 107
            || errno == 111
            || errno == 113
            || errno == 10051
            || errno == 10057
            || errno == 10061
            || errno == 10065;
    }

    public static void TryDisposeQuietly(IDisposable? disposable)
    {
        if (disposable == null)
            return;

        try
        {
            disposable.Dispose();
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[perf-shared] dispose failed: {ex.Message}");
        }
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
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[perf-shared] delete file failed: {ex.Message}");
        }
    }

    private static int InitializePortSeed()
    {
        return Random.Shared.Next(20000, 40001);
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
