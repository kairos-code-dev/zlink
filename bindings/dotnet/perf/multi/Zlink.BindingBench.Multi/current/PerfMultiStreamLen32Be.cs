using System;
using System.Collections.Generic;
using System.Threading;
using Zlink;

internal static partial class PerfRunner
{
    private static int RunMultiStreamLen32BeServer(string transport, int size)
    {
        size = Math.Max(1, size);
        int rcvTimeoutMs = ParsePositiveEnv("PERF_MULTI_RCVTIMEO_MS", 5000);
        string endpoint = MultiEndpointFor(transport,
            "multi-MULTI_STREAM_LEN32BE");

        using var ctx = new Context();
        ApplyMultiServerContextOptions(ctx);
        using var server = new Zlink.Socket(ctx, Zlink.SocketType.Stream);
        ApplyMultiSocketOptions(server);
        ConfigureTlsServerIfNeeded(server, transport);
        server.SetOption(SocketOptions.RcvTimeo, rcvTimeoutMs);

        int stopRequested = 0;
        int callbackFailed = 0;
        long payloadSeen = 0;
        long lastActivityTicks = System.Diagnostics.Stopwatch.GetTimestamp();

        StreamBatchHandler len32BeHandler = (rid, messages) =>
        {
            for (int i = 0; i < messages.Length; i++)
            {
                Message message = messages[i];
                ReadOnlySpan<byte> payload = message.AsReadOnlySpan();
                if (payload.Length == 1
                    && (payload[0] == 0x00 || payload[0] == 0x01))
                {
                    message.Dispose();
                    continue;
                }

                if (payload.SequenceEqual(MultiStopToken))
                {
                    Interlocked.Exchange(ref stopRequested, 1);
                    message.Dispose();
                    continue;
                }

                Interlocked.Increment(ref payloadSeen);
                Interlocked.Exchange(ref lastActivityTicks,
                    System.Diagnostics.Stopwatch.GetTimestamp());

                try
                {
                    server.StreamSend(rid, message, SendFlags.None);
                }
                catch
                {
                    Interlocked.Exchange(ref callbackFailed, 1);
                    Interlocked.Exchange(ref stopRequested, 1);
                }
            }
            return 0;
        };

        server.AttachStreamLen32Be(len32BeHandler);
        server.Bind(endpoint);
        Console.WriteLine($"READY,{endpoint}");

        long idleBreakTicks = (long)(System.Diagnostics.Stopwatch.Frequency
            * (Math.Max(rcvTimeoutMs * 2, 5000) / 1000.0));
        while (Volatile.Read(ref stopRequested) == 0)
        {
            if (Volatile.Read(ref callbackFailed) != 0)
                return 2;
            if (Volatile.Read(ref payloadSeen) > 0)
            {
                long idleTicks = System.Diagnostics.Stopwatch.GetTimestamp()
                    - Volatile.Read(ref lastActivityTicks);
                if (idleTicks >= idleBreakTicks)
                    break;
            }
            Thread.Sleep(1);
        }

        try
        {
            server.DetachStream();
        }
        catch
        {
        }

        return Volatile.Read(ref callbackFailed) == 0 ? 0 : 2;
    }

    private static int RunMultiStreamLen32BeClient(string transport, int size,
        string endpoint)
    {
        size = Math.Max(1, size);
        int warmupSeconds = ParseNonNegativeEnv("PERF_MULTI_WARMUP_SECONDS", 3);
        int durationSeconds = ParsePositiveEnv("PERF_MULTI_DURATION_SECONDS", 5);
        int settleMs = ParseNonNegativeEnv("PERF_MULTI_SETTLE_MS", 500);
        int drainMs = ResolveMultiDrainMs("MULTI_STREAM_LEN32BE");
        int sizeTransitionDrainMs = ParseNonNegativeEnv(
            "PERF_MULTI_SIZE_TRANSITION_DRAIN_MS", 300);
        bool activeWarmup =
            ParseNonNegativeEnv("PERF_MULTI_ACTIVE_WARMUP", 0) == 1;
        int warmupDrainMs = ParseNonNegativeEnv("PERF_MULTI_WARMUP_DRAIN_MS",
            Math.Max(drainMs, 1000));
        int sndTimeoutMs = ParsePositiveEnv("PERF_MULTI_SNDTIMEO_MS", 5000);
        int rcvTimeoutMs = ParsePositiveEnv("PERF_MULTI_RCVTIMEO_MS", 5000);

        int defaultClients = 1000;
        int clientCount = Math.Max(1,
            ParsePositiveEnv("PERF_MULTI_CLIENTS", defaultClients));

        var streamClients = new List<RawTransportStreamClient>(clientCount);
        try
        {
            for (int i = 0; i < clientCount; i++)
            {
                var client = new RawTransportStreamClient(endpoint,
                    sndTimeoutMs, rcvTimeoutMs);
                client.Connect();
                streamClients.Add(client);
            }

            if (streamClients.Count == 0)
            {
                Console.Error.WriteLine("multi_client_error:no_ready_connections");
                return 2;
            }

            var payloadBody = new byte[size];
            Array.Fill(payloadBody, (byte)'a');
            int index = 0;

            if (activeWarmup)
            {
                var warmupDeadline = DateTime.UtcNow.AddSeconds(
                    Math.Max(0, warmupSeconds));
                while (DateTime.UtcNow < warmupDeadline)
                {
                    RawTransportStreamClient client = streamClients[index];
                    client.SendFrame(payloadBody);
                    _ = client.ReceiveFramePayload();
                    index = (index + 1) % streamClients.Count;
                }
                if (warmupDrainMs > 0)
                    Thread.Sleep(warmupDrainMs);
            }
            else if (warmupSeconds > 0)
            {
                Thread.Sleep(warmupSeconds * 1000);
            }

            if (settleMs > 0)
                Thread.Sleep(settleMs);

            long measureCount = 0;
            var sw = System.Diagnostics.Stopwatch.StartNew();
            var benchDeadline = DateTime.UtcNow.AddSeconds(
                Math.Max(1, durationSeconds));
            while (DateTime.UtcNow < benchDeadline)
            {
                RawTransportStreamClient client = streamClients[index];
                client.SendFrame(payloadBody);
                _ = client.ReceiveFramePayload();
                measureCount++;
                index = (index + 1) % streamClients.Count;
            }
            sw.Stop();

            if (drainMs > 0)
                Thread.Sleep(drainMs);
            if (sizeTransitionDrainMs > 0)
                Thread.Sleep(sizeTransitionDrainMs);

            try
            {
                streamClients[0].SendFrame(MultiStopToken);
            }
            catch
            {
            }

            double throughput = sw.Elapsed.TotalSeconds > 0.0
                ? measureCount / sw.Elapsed.TotalSeconds
                : 0.0;
            double latencyUs = (sw.Elapsed.TotalMilliseconds * 1000.0)
                / Math.Max(1.0, measureCount * 2.0);

            PrintResult("MULTI_STREAM_LEN32BE", transport, size, throughput,
                latencyUs);
            return 0;
        }
        finally
        {
            foreach (RawTransportStreamClient client in streamClients)
            {
                try
                {
                    client.Dispose();
                }
                catch
                {
                }
            }
        }
    }
}
