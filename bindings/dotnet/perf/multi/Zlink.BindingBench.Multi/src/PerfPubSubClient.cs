using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfPubSubClient
{
    internal static int Run(PerfOptions options)
    {
        int size = Math.Max(1, options.Size);
        int warmupSeconds = ResolveMultiWarmupSeconds(options);
        int durationSeconds = ResolveMultiDurationSeconds(options);
        bool activeWarmup = ResolveMultiActiveWarmup(options);
        int sndTimeoutMs = ResolveMultiSndTimeoutMs(options);
        int rcvTimeoutMs = ResolveMultiRcvTimeoutMs(options);
        int readyTimeoutMs = ResolveMultiConnectReadyTimeoutMs(options);
        int latencySampleCap = ResolveMultiLatencySampleCap(options);
        int clientCount = ResolveMultiClients(options);
        int pollTimeoutMs = ResolveEffectiveMultiClientPollTimeoutMs(options);
        string endpoint = options.Endpoint;

        using var ctx = new Context();
        using var pollManager = new PollManager();
        ApplyMultiClientContextOptions(ctx, options);
        var clients = new List<SocketBase>(clientCount);
        var monitors = new List<MonitorSocket>(clientCount);
        try
        {
            for (int i = 0; i < clientCount; i++)
            {
                var client = new SubSocket(ctx);
                ApplyMultiSocketOptions(client, options);
                ConfigureTlsClientIfNeeded(client, options.Transport);
                client.SetOption(SocketOptions.SndTimeo, sndTimeoutMs);
                client.SetOption(SocketOptions.RcvTimeo, rcvTimeoutMs);
                client.SetOption(SocketOptions.Subscribe, string.Empty);
                var monitor = client.MonitorOpen(SocketEvent.ConnectionReady);
                client.Connect(endpoint);
                clients.Add(client);
                monitors.Add(monitor);
            }

            List<SocketBase> activeClients = WaitAllClientConnectReady(
                pollManager, clients, monitors, readyTimeoutMs);
            if (activeClients.Count != clients.Count)
            {
                Console.Error.WriteLine("multi_client_error:no_ready_connections");
                return 2;
            }
            DisposeAllQuietly(monitors);
            monitors.Clear();

            var recv = new byte[Math.Max(256, Math.Max(size, MultiStopToken.Length))];
            var result = RunMultiPubSubClientLoop(pollManager, activeClients,
                recv, size, latencySampleCap, pollTimeoutMs,
                warmupSeconds, durationSeconds, activeWarmup);

            PrintResult(options.Pattern, options.Transport, size, result.throughput,
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
        RunMultiPubSubClientLoop(PollManager pollManager,
            List<SocketBase> activeClients,
            byte[] recv, int msgSize, int latencySampleCap, int pollTimeoutMs,
            int warmupSeconds, int durationSeconds, bool activeWarmup)
    {
        const uint expectedRunId = 1;
        var latSamples = new List<double>(latencySampleCap);
        long sampleSeen = 0;
        uint rng = 0xA341316Cu;
        long measureCount = 0;

        _ = activeWarmup;
        if (warmupSeconds > 0)
        {
            long warmupDeadlineTicks = Stopwatch.GetTimestamp()
                + (long)Math.Max(0, warmupSeconds) * Stopwatch.Frequency;
            while (Stopwatch.GetTimestamp() < warmupDeadlineTicks)
            {
                if (PollSocketReadReady(pollManager, activeClients,
                        pollTimeoutMs) <= 0)
                    continue;

                for (int i = 0; i < activeClients.Count; i++)
                {
                    if (!IsSocketReadReady(pollManager, i))
                        continue;
                    DrainReadableSocket(activeClients[i], recv.AsSpan(),
                        static _ => true);
                }
            }

        }

        long benchDeadlineTicks = Stopwatch.GetTimestamp()
            + (long)Math.Max(1, durationSeconds) * Stopwatch.Frequency;
        while (Stopwatch.GetTimestamp() < benchDeadlineTicks)
        {
            if (PollSocketReadReady(pollManager, activeClients,
                    pollTimeoutMs) <= 0)
            {
                continue;
            }

            for (int i = 0; i < activeClients.Count; i++)
            {
                if (!IsSocketReadReady(pollManager, i))
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
