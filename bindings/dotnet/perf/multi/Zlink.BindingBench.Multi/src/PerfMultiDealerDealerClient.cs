using System;
using System.Collections.Generic;
using System.Diagnostics;
using Systems.Zlink;
using static PerfRunner;

internal static class PerfMultiDealerDealerClient
{
    internal static int Run(PerfOptions options)
    {
        int size = Math.Max(1, options.Size);
        int sndTimeoutMs = ResolveMultiSndTimeoutMs(options);
        int rcvTimeoutMs = ResolveMultiRcvTimeoutMs(options);
        int readyTimeoutMs = ResolveMultiConnectReadyTimeoutMs(options);
        int latencySampleCap = ResolveMultiLatencySampleCap(options);
        int clientCount = ResolveMultiClients(options);
        int durationSeconds = ResolveMultiDurationSeconds(options);
        string endpoint = options.Endpoint;

        using var ctx = new Context();
        using var pollManager = new PollManager();
        using var controlState = new RunnerControlState(size);
        ApplyMultiClientContextOptions(ctx, options);
        var clients = new List<SocketBase>(clientCount);
        var monitors = new List<MonitorSocket>(clientCount);
        try
        {
            for (int i = 0; i < clientCount; i++)
            {
                var client = new DealerSocket(ctx);
                ApplyMultiSocketOptions(client, options);
                ConfigureTlsClientIfNeeded(client, options.Transport);
                client.Options.SendTimeout = TimeSpan.FromMilliseconds(sndTimeoutMs);
                client.Options.ReceiveTimeout = TimeSpan.FromMilliseconds(rcvTimeoutMs);
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
            WriteStdoutLine($"CLIENT_READY,{size}");

            if (!controlState.WaitForStart(readyTimeoutMs))
            {
                if (!controlState.StopRequested)
                    Console.Error.WriteLine("multi_client_error:no_start");
                return controlState.StopRequested ? 0 : 2;
            }

            var result = RunReceivePhase(pollManager, activeClients, size,
                latencySampleCap, durationSeconds);
            if (result.measureCount <= 0)
                return 2;

            PrintResult(options.Pattern, options.Transport, size, result.throughput,
                result.latencyNs, result.latencyP95Ns, result.latencyP99Ns);
            return 0;
        }
        finally
        {
            DisposeAllQuietly(monitors);
            DisposeAllQuietly(clients);
        }
    }

    private static (double throughput, double latencyNs, double latencyP95Ns,
        double latencyP99Ns, long measureCount)
        RunReceivePhase(PollManager pollManager, List<SocketBase> activeClients,
            int msgSize, int latencySampleCap, int durationSeconds)
    {
        const uint expectedRunId = 1;
        var latSamples = new List<double>(latencySampleCap);
        long sampleSeen = 0;
        uint rng = 0xA341316Cu;
        long measureCount = 0;

        long benchDeadlineTicks = Stopwatch.GetTimestamp()
            + (long)Math.Max(1, durationSeconds) * Stopwatch.Frequency;
        while (Stopwatch.GetTimestamp() < benchDeadlineTicks)
        {
            int timeoutMs = RemainingMilliseconds(benchDeadlineTicks);
            if (timeoutMs <= 0)
                break;

            if (PollSocketReadReady(pollManager, activeClients, timeoutMs) <= 0)
                continue;

            for (int i = 0; i < activeClients.Count; i++)
            {
                if (!IsSocketReadReady(pollManager, i))
                    continue;

                while (true)
                {
                    using Received? received = TryRecvNoWait(
                        (DealerSocket)activeClients[i]);
                    if (received == null || received.Count == 0)
                        break;

                    ReadOnlySpan<byte> body = received.SinglePartOrThrow()
                        .AsReadOnlySpan();
                    if (IsStopTokenPayload(body))
                        continue;

                    long recvTicks = Stopwatch.GetTimestamp();
                    if (recvTicks > benchDeadlineTicks)
                        continue;

                    if (!PerfShared.TryDecodeMetricHeader(body, out PerfMetricHeader header)
                        || header.RunId != expectedRunId
                        || header.MsgSize != (uint)msgSize
                        || header.Phase != (uint)PerfPhase.Active)
                    {
                        continue;
                    }

                    measureCount++;
                    ulong nowNs = EpochNs();
                    if (header.SentTsNs > 0 && nowNs >= header.SentTsNs)
                    {
                        double sampleLatencyNs = nowNs - header.SentTsNs;
                        ReservoirSample(latSamples, sampleLatencyNs,
                            ref sampleSeen, latencySampleCap, ref rng);
                    }
                }
            }
        }

        double configuredSeconds = Math.Max(1.0, durationSeconds);
        double throughput = measureCount / configuredSeconds;
        double fallbackLatencyNs = (configuredSeconds * 1_000_000_000.0)
            / Math.Max(1.0, measureCount);
        var latency = ComputeLatencyStats(latSamples);
        double latencyNs = latency.mean > 0.0 ? latency.mean : fallbackLatencyNs;
        double latencyP95Ns = latency.p95 > 0.0 ? latency.p95 : latencyNs;
        double latencyP99Ns = latency.p99 > 0.0 ? latency.p99 : latencyP95Ns;

        return (throughput, latencyNs, latencyP95Ns, latencyP99Ns, measureCount);
    }

    private static Received? TryRecvNoWait(DealerSocket socket)
    {
        return socket.Recv(RecvFlags.DontWait);
    }

    private static int RemainingMilliseconds(long deadlineTicks)
    {
        long nowTicks = Stopwatch.GetTimestamp();
        if (deadlineTicks <= nowTicks)
            return 0;

        double remainingMs = (deadlineTicks - nowTicks) * 1000.0
            / Stopwatch.Frequency;
        if (remainingMs >= int.MaxValue)
            return int.MaxValue;
        return (int)Math.Ceiling(remainingMs);
    }
}
