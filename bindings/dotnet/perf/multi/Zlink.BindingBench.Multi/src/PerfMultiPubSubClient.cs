using System;
using System.Collections.Generic;
using System.Diagnostics;
using Systems.Zlink;
using static PerfRunner;

internal static class PerfMultiPubSubClient
{
    internal static int Run(PerfOptions options)
    {
        int size = Math.Max(1, options.Size);
        int sndTimeoutMs = ResolveMultiSndTimeoutMs(options);
        int rcvTimeoutMs = ResolveMultiRcvTimeoutMs(options);
        int readyTimeoutMs = ResolveMultiConnectReadyTimeoutMs(options);
        int latencySampleCap = ResolveMultiLatencySampleCap(options);
        int clientCount = ResolveMultiClients(options);
        int pollTimeoutMs = ResolveMultiClientPollTimeoutMs(options);
        int durationSeconds = ResolveMultiDurationSeconds(options);
        string endpoint = options.Endpoint;

        using var ctx = new Context();
        using var pollManager = new PollManager();
        using var controlState = new RunnerControlState(size,
            requirePhaseActive: true);
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
                client.Options.SendTimeout = TimeSpan.FromMilliseconds(sndTimeoutMs);
                client.Options.ReceiveTimeout = TimeSpan.FromMilliseconds(rcvTimeoutMs);
                client.SetSubscription(string.Empty);
                var monitor = client.MonitorOpen(SocketEvent.ConnectionReady);
                client.Connect(endpoint);
                clients.Add(client);
                monitors.Add(monitor);
            }

            List<SocketBase> activeClients = WaitClientConnectReadyAll(
                pollManager, clients, monitors, readyTimeoutMs);
            if (activeClients.Count != clients.Count)
            {
                Console.Error.WriteLine("multi_client_error:no_ready_connections");
                return 2;
            }
            DisposeAllQuietly(monitors);
            monitors.Clear();

            for (int i = 0; i < clients.Count; i++)
                ApplyAutoHwmMsgUnit(clients[i], size);
            RecalculateAutoHwm(ctx);

            WriteStdoutLine($"CLIENT_READY,{size}");

            if (!controlState.WaitForStart(readyTimeoutMs))
            {
                if (!controlState.StopRequested)
                    Console.Error.WriteLine("multi_client_error:no_start");
                return controlState.StopRequested ? 0 : 2;
            }

            var result = RunMultiPubSubClientLoop(pollManager, activeClients,
                size, latencySampleCap, pollTimeoutMs, durationSeconds);

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
        RunMultiPubSubClientLoop(PollManager pollManager,
            List<SocketBase> activeClients, int msgSize, int latencySampleCap,
            int pollTimeoutMs,
            int durationSeconds)
    {
        const uint expectedRunId = 1;
        var latSamples = new List<double>(latencySampleCap);
        long sampleSeen = 0;
        uint rng = 0xA341316Cu;
        long measureCount = 0;

        long benchDeadlineTicks = Stopwatch.GetTimestamp()
            + (long)Math.Max(1, durationSeconds) * Stopwatch.Frequency;

        // PERF_MULTI_TEST_POLICY § 1.3.1: poller wait timeout is -1
        // (signal-driven). The loop exits when each client has received
        // a wire-level stop token from the publisher.
        var stoppedClients = new bool[activeClients.Count];
        int stoppedCount = 0;
        while (stoppedCount < activeClients.Count)
        {
            int readyCount = PollSocketReadReady(pollManager, activeClients,
                pollTimeoutMs);
            if (readyCount <= 0)
            {
                continue;
            }

            for (int readyOffset = 0; readyOffset < readyCount; readyOffset++)
            {
                int i = ReadySocketIndexAt(pollManager, readyOffset);
                if (stoppedClients[i] || !IsSocketReadReady(pollManager, i))
                    continue;

                while (true)
                {
                    TopicMessage? subscribed = TrySubscribeNoWait(
                        (SubSocket)activeClients[i]);
                    if (subscribed == null)
                        break;

                    using (subscribed)
                    {
                        ReadOnlySpan<byte> body = subscribed.FirstPart()
                            .AsReadOnlySpan();
                        if (IsStopTokenPayload(body))
                        {
                            stoppedClients[i] = true;
                            stoppedCount++;
                            break;
                        }

                        long recvTicks = Stopwatch.GetTimestamp();
                        if (recvTicks > benchDeadlineTicks)
                            continue;

                        bool headerOk = PerfRunner.TryDecodeMetricHeader(body,
                            out PerfMetricHeader header);
                        if (!headerOk
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

    private static TopicMessage? TrySubscribeNoWait(SubSocket socket)
    {
        try
        {
            return socket.Subscribe(RecvFlags.DontWait);
        }
        catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                        || IsInterrupted(ex.InternalErrno))
        {
            return null;
        }
    }
}
