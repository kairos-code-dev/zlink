using System;
using System.Collections.Generic;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfPubSubClient
{
    internal static int Run(string transport, int size,
        string endpoint)
    {
        const string pattern = "PUBSUB";
        size = Math.Max(1, size);
        int warmupSeconds = ResolveMultiWarmupSeconds();
        int durationSeconds = ResolveMultiDurationSeconds();
        int settleMs = ResolveMultiSettleMs();
        int drainMs = ResolveMultiDrainMs(pattern);
        int sizeTransitionDrainMs = ResolveMultiSizeTransitionDrainMs();
        bool activeWarmup = ResolveMultiActiveWarmup();
        int warmupDrainMs = ResolveMultiWarmupDrainMs(drainMs);
        int sndTimeoutMs = ResolveMultiSndTimeoutMs();
        int rcvTimeoutMs = ResolveMultiRcvTimeoutMs();
        int readyTimeoutMs = ResolveMultiConnectReadyTimeoutMs();
        int latencySampleCap = ResolveMultiLatencySampleCap();
        int clientCount = ResolveMultiClients(pattern);

        using var ctx = new Context();
        ApplyMultiClientContextOptions(ctx);
        var clients = new List<Zlink.Socket>(clientCount);
        var monitors = new List<MonitorSocket>(clientCount);
        try
        {
            for (int i = 0; i < clientCount; i++)
            {
                var client = new Zlink.Socket(ctx, Zlink.SocketType.Sub);
                ApplyMultiSocketOptions(client, pattern);
                ConfigureTlsClientIfNeeded(client, transport);
                client.SetOption(SocketOptions.SndTimeo, sndTimeoutMs);
                client.SetOption(SocketOptions.RcvTimeo, rcvTimeoutMs);
                client.SetOption(SocketOptions.Subscribe, string.Empty);
                var monitor = client.MonitorOpen(SocketEvent.ConnectionReady);
                client.Connect(endpoint);
                clients.Add(client);
                monitors.Add(monitor);
            }

            List<Zlink.Socket> activeClients = WaitAllClientConnectReady(clients,
                monitors, readyTimeoutMs);
            if (activeClients.Count != clients.Count)
            {
                Console.Error.WriteLine("multi_client_error:no_ready_connections");
                return 2;
            }
            DisposeAllQuietly(monitors);
            monitors.Clear();

            var recv = new byte[Math.Max(256, Math.Max(size, MultiStopToken.Length))];
            var result = RunMultiPubSubClientLoop(activeClients, recv,
                size, latencySampleCap,
                warmupSeconds, durationSeconds, settleMs, drainMs,
                sizeTransitionDrainMs, activeWarmup, warmupDrainMs);

            PrintResult(pattern, transport, size, result.throughput,
                result.latencyUs, result.latencyP95Us, result.latencyP99Us);
            return 0;
        }
        finally
        {
            DisposeAllQuietly(monitors);
            DisposeAllQuietly(clients);
        }
    }

    private static (double throughput, double latencyUs, double latencyP95Us,
        double latencyP99Us)
        RunMultiPubSubClientLoop(List<Zlink.Socket> activeClients,
            byte[] recv, int msgSize, int latencySampleCap, int warmupSeconds,
            int durationSeconds, int settleMs, int drainMs,
            int sizeTransitionDrainMs, bool activeWarmup, int warmupDrainMs)
    {
        const uint expectedRunId = 1;
        var latSamples = new List<double>(latencySampleCap);
        long sampleSeen = 0;
        uint rng = 0xA341316Cu;
        long measureCount = 0;

        _ = activeWarmup;
        if (warmupSeconds > 0)
        {
            DateTime warmupDeadline = DateTime.UtcNow.AddSeconds(
                Math.Max(0, warmupSeconds));
            while (DateTime.UtcNow < warmupDeadline)
            {
                int remainingMs = (int)Math.Max(0,
                    (warmupDeadline - DateTime.UtcNow).TotalMilliseconds);
                if (PollSocketReadReady(activeClients, remainingMs) <= 0)
                    continue;

                for (int i = 0; i < activeClients.Count; i++)
                {
                    if (!IsSocketReadReady(i))
                        continue;
                    DrainReadableSocket(activeClients[i], recv.AsSpan(),
                        static _ => true);
                }
            }

            if (warmupDrainMs > 0)
                Thread.Sleep(warmupDrainMs);
        }

        if (settleMs > 0)
            Thread.Sleep(settleMs);

        DateTime benchDeadline = DateTime.UtcNow.AddSeconds(
            Math.Max(1, durationSeconds));
        while (DateTime.UtcNow < benchDeadline)
        {
            int remainingMs = (int)Math.Max(0,
                (benchDeadline - DateTime.UtcNow).TotalMilliseconds);
            if (PollSocketReadReady(activeClients, remainingMs) <= 0)
            {
                continue;
            }

            for (int i = 0; i < activeClients.Count; i++)
            {
                if (!IsSocketReadReady(i))
                    continue;

                DrainReadableSocket(activeClients[i], recv.AsSpan(), body =>
                {
                    bool headerOk = TryDecodeMetricHeader(body,
                        out PerfMetricHeader header);
                    if (!headerOk
                        || header.RunId != expectedRunId
                        || header.MsgSize != (uint)msgSize)
                    {
                        return true;
                    }

                    if (header.Phase == (uint)PerfPhase.Active)
                    {
                        measureCount++;
                        ulong nowUs = EpochUs();
                        if (header.SentTsUs > 0 && nowUs >= header.SentTsUs)
                        {
                            double sampleLatencyUs = nowUs - header.SentTsUs;
                            ReservoirSample(latSamples, sampleLatencyUs,
                                ref sampleSeen, latencySampleCap, ref rng);
                        }
                    }
                    return true;
                });
            }
        }

        if (drainMs > 0)
            Thread.Sleep(drainMs);
        if (sizeTransitionDrainMs > 0)
            Thread.Sleep(sizeTransitionDrainMs);

        double configuredSeconds = Math.Max(1.0, durationSeconds);
        double throughput = measureCount / configuredSeconds;
        double fallbackLatencyUs = (configuredSeconds * 1_000_000.0)
            / Math.Max(1.0, measureCount);
        var latency = ComputeLatencyStats(latSamples);
        double latencyUs = latency.mean > 0.0 ? latency.mean : fallbackLatencyUs;
        double latencyP95Us = latency.p95 > 0.0 ? latency.p95 : latencyUs;
        double latencyP99Us = latency.p99 > 0.0 ? latency.p99 : latencyP95Us;

        return (throughput, latencyUs, latencyP95Us, latencyP99Us);
    }
}
