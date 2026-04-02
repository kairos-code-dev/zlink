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
    private const int ErrnoEintr = 4;
    private const int ErrnoEagain = 11;
    internal const int SingleSettleTimeMs = 100;
    internal const int SingleConnectWaitMs = 300;
    private static readonly object IpcLock = new();
    private static readonly HashSet<string> IpcPaths = new();
    private static bool IpcCleanupHooked;

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
            catch (ZlinkException ex) when (IsWouldBlock(ex.Errno)
                                            && (flags & ReceiveFlags.DontWait) != 0)
            {
                return 0;
            }
        }
    }

    internal static int ReceiveBlocking(Zlink.Socket socket, byte[] buffer,
        ReceiveFlags flags = ReceiveFlags.None)
    {
        if (buffer == null)
            throw new ArgumentNullException(nameof(buffer));
        return ReceiveBlocking(socket, buffer.AsSpan(), flags);
    }

    internal static int TryReceiveNonBlocking(Zlink.Socket socket, Span<byte> buffer)
    {
        while (true)
        {
            try
            {
                return socket.Receive(buffer, ReceiveFlags.DontWait);
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

    internal static int DrainRemainingFramesNonBlocking(Zlink.Socket socket)
    {
        int drained = 0;
        Span<byte> discard = stackalloc byte[256];
        while (socket.GetOption(SocketOptions.RcvMore) != 0)
        {
            int n = TryReceiveNonBlocking(socket, discard);
            if (n <= 0)
                break;
            drained += n;
        }

        return drained;
    }

    internal static int SendBlocking(Zlink.Socket socket, ReadOnlySpan<byte> buffer,
        SendFlags flags = SendFlags.None)
    {
        return socket.Send(buffer, flags);
    }

    internal static int SendBlocking(Zlink.Socket socket, byte[] buffer,
        SendFlags flags = SendFlags.None)
    {
        if (buffer == null)
            throw new ArgumentNullException(nameof(buffer));
        return socket.Send(buffer, flags);
    }

    internal static bool WaitForInput(Poller poller, Span<PollEvent> events,
        int timeoutMs)
    {
        int written = poller.Wait(events, timeoutMs, out int totalReady);
        return written > 0 || totalReady > 0;
    }

    internal static bool WaitUntil(Func<bool> check, int timeoutMs,
        int intervalMs = 1)
    {
        long deadlineTicks = DeadlineTicksFromMilliseconds(timeoutMs);
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

    internal static void PrintResult(string pattern, string transport, int size,
        double thr, double latUs)
    {
        PrintResult(pattern, transport, size, thr, latUs, latUs, latUs);
    }

    internal static void PrintResult(string pattern, string transport, int size,
        double thr, double latUs, double latP95Us, double latP99Us)
    {
        double bw = BandwidthMbps(thr, size);
        double latMs = UsToMs(latUs);
        double latP95Ms = UsToMs(latP95Us);
        double latP99Ms = UsToMs(latP99Us);
        Console.WriteLine($"RESULT,current,{pattern},{transport},{size},throughput,{thr}");
        Console.WriteLine($"RESULT,current,{pattern},{transport},{size},bandwidth,{bw}");
        Console.WriteLine($"RESULT,current,{pattern},{transport},{size},latency,{latMs}");
        Console.WriteLine(
            $"RESULT,current,{pattern},{transport},{size},latency_p95,{latP95Ms}");
        Console.WriteLine(
            $"RESULT,current,{pattern},{transport},{size},latency_p99,{latP99Ms}");
    }

    internal static int PrintUnsupported(string pattern, string transport,
        int size, string reason)
    {
        Console.WriteLine($"UNSUPPORTED,{pattern},{transport},{size},{reason}");
        return 0;
    }

    private static double BandwidthMbps(double throughput, int size)
    {
        return (throughput * size) / 1_000_000.0;
    }

    private static double UsToMs(double latencyUs)
    {
        return latencyUs / 1000.0;
    }

    internal static int ParseEnv(string name, int defaultValue)
    {
        var v = Environment.GetEnvironmentVariable(name);
        return int.TryParse(v, out var p) && p > 0 ? p : defaultValue;
    }

    internal static int ParseEnvNonNegative(string name, int defaultValue)
    {
        var v = Environment.GetEnvironmentVariable(name);
        return int.TryParse(v, out var p) && p >= 0 ? p : defaultValue;
    }

    private static int ResolveSingleHwmValue(string specificName)
    {
        int hwm = ParseEnv("PERF_SINGLE_HWM", 1000);
        int specific = ParseEnv(specificName, 0);
        return specific > 0 ? specific : hwm;
    }

    private static int ResolveSingleMaxSockets()
    {
        int explicitMaxSockets = ParseEnv("PERF_MAX_SOCKETS", 0);
        if (explicitMaxSockets > 0)
            return explicitMaxSockets;

        int clients = ParseEnv("PERF_CLIENTS", 0);
        if (clients <= 0)
            return 0;

        long required = clients + 4096L;
        if (required > int.MaxValue)
            return int.MaxValue;
        return (int)required;
    }

    internal static void ApplySingleContextOptions(Context ctx)
    {
        int ioThreads = ParseEnvNonNegative("PERF_IO_THREADS", 0);
        if (ioThreads > 0)
            ctx.SetOption(ContextOption.IoThreads, ioThreads);

        int maxSockets = ResolveSingleMaxSockets();
        if (maxSockets > 0)
            ctx.SetOption(ContextOption.MaxSockets, maxSockets);
    }

    internal static void ApplySingleSocketOptions(Zlink.Socket socket)
    {
        int sndHwm = ResolveSingleHwmValue("PERF_SINGLE_SNDHWM");
        int rcvHwm = ResolveSingleHwmValue("PERF_SINGLE_RCVHWM");

        int sndTimeo = ParseEnvNonNegative("PERF_SINGLE_SNDTIMEO_MS", 200);
        int rcvTimeo = ParseEnvNonNegative("PERF_SINGLE_RCVTIMEO_MS", 200);

        socket.SetOption(SocketOptions.Linger, 0);
        socket.SetOption(SocketOptions.SndHwm, sndHwm);
        socket.SetOption(SocketOptions.RcvHwm, rcvHwm);
        socket.SetOption(SocketOptions.SndTimeo, sndTimeo);
        socket.SetOption(SocketOptions.RcvTimeo, rcvTimeo);
    }

    internal static int ResolveSingleWarmupCount(string pattern)
    {
        int fallback = pattern.Equals("SPOT", StringComparison.OrdinalIgnoreCase)
            ? 200
            : 1000;
        return ParseEnv("PERF_WARMUP_COUNT", fallback);
    }

    internal static int ResolveSingleLatencyCount(string pattern)
    {
        int fallback = pattern.Equals("SPOT", StringComparison.OrdinalIgnoreCase)
            ? 200
            : 500;
        return ParseEnv("PERF_LAT_COUNT", fallback);
    }

    internal static int ResolveSpotDiscoveryTimeoutMs()
    {
        return ParseEnvNonNegative("PERF_SPOT_DISCOVERY_TIMEOUT_MS", 4000);
    }

    internal static int ResolveSpotReadyTimeoutMs()
    {
        return ParseEnvNonNegative("PERF_SPOT_READY_TIMEOUT_MS", 2000);
    }

    internal static bool IsSecureTransport(string transport)
    {
        return transport.Equals("tls", StringComparison.OrdinalIgnoreCase)
               || transport.Equals("wss", StringComparison.OrdinalIgnoreCase);
    }

    internal static long TimestampUs()
    {
        long ts = Stopwatch.GetTimestamp();
        return (long)(ts * (1_000_000.0 / Stopwatch.Frequency));
    }

    internal static long DeadlineTicksFromSeconds(int seconds)
    {
        int boundedSeconds = Math.Max(1, seconds);
        return Stopwatch.GetTimestamp()
            + ((long)boundedSeconds * Stopwatch.Frequency);
    }

    internal static long DeadlineTicksFromMilliseconds(int milliseconds)
    {
        long boundedMs = Math.Max(1, milliseconds);
        long deltaTicks = (boundedMs * Stopwatch.Frequency) / 1000;
        if (deltaTicks <= 0)
            deltaTicks = 1;
        return Stopwatch.GetTimestamp() + deltaTicks;
    }

    internal static double ElapsedSecondsFromTicks(long startTicks,
        long endTicks)
    {
        long deltaTicks = Math.Max(0, endTicks - startTicks);
        return deltaTicks / (double)Stopwatch.Frequency;
    }

    internal static void StampHeader(Span<byte> header, long tsUs)
    {
        if (header.Length < 8)
            throw new ArgumentException("header length must be >= 8", nameof(header));

        BinaryPrimitives.WriteInt64LittleEndian(header, tsUs);
    }

    internal static long DecodeHeader(ReadOnlySpan<byte> header)
    {
        if (header.Length < 8)
            throw new ArgumentException("header length must be >= 8", nameof(header));

        return BinaryPrimitives.ReadInt64LittleEndian(header);
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

    internal static void PrintSingleProcessMetrics(string pattern, string transport,
        int size, TimeSpan cpuStart, Stopwatch wall)
    {
        var process = Process.GetCurrentProcess();
        process.Refresh();
        TimeSpan cpuEnd = process.TotalProcessorTime;
        double cpuSec = Math.Max(0.0, (cpuEnd - cpuStart).TotalSeconds);
        double wallSec = Math.Max(1e-9, wall.Elapsed.TotalSeconds);
        double ncpu = Math.Max(1, Environment.ProcessorCount);
        double cpuPct = (cpuSec / (wallSec * ncpu)) * 100.0;
        double memMb = process.WorkingSet64 / (1024.0 * 1024.0);

        Console.WriteLine($"RESULT,current,{pattern},{transport},{size},cpu_pct,{cpuPct}");
        Console.WriteLine($"RESULT,current,{pattern},{transport},{size},mem_mb,{memMb}");
        Console.WriteLine($"RESULT,current,{pattern},{transport},{size},snd_pending_max,0");
        Console.WriteLine($"RESULT,current,{pattern},{transport},{size},rcv_pending_max,0");
        Console.WriteLine($"RESULT,current,{pattern},{transport},{size},rcv_pending_end,0");
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

    internal static void TryDisposeQuietly(IDisposable? disposable)
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

    internal static void TryDisposeAllQuietly(params IDisposable?[] disposables)
    {
        if (disposables == null)
            return;

        foreach (IDisposable? disposable in disposables)
            TryDisposeQuietly(disposable);
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
