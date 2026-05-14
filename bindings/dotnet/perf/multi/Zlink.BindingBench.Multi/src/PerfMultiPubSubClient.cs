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
            if (clients.Count > 0)
                PrintAutoHwmSnapshot(clients[0], "endpoint",
                    options.Transport, size);

            WriteStdoutLine($"CLIENT_READY,{size}");

            if (!controlState.WaitForStart(readyTimeoutMs))
            {
                if (!controlState.StopRequested)
                    Console.Error.WriteLine("multi_client_error:no_start");
                return controlState.StopRequested ? 0 : 2;
            }

            var result = RunMultiPubSubClientLoop(pollManager, activeClients,
                size, latencySampleCap, durationSeconds,
                ResolveMultiClientPollTimeoutMs(options));

            if (result.measureCount <= 0)
                return 2;

            PrintResult(options.Pattern, options.Transport, size, result.throughput,
                result.latencyNs, result.latencyP95Ns, result.latencyP99Ns);
            WriteStdoutLine($"CLIENT_DONE,{size}");
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
            int durationSeconds, int pollTimeoutMs)
    {
        _ = pollManager;
        _ = pollTimeoutMs;
        const uint expectedRunId = 1;
        var activeLatSamples = new List<double>(latencySampleCap);
        long activeSampleSeen = 0;
        uint rng = 0xA341316Cu;
        long measureCount = 0;

        long benchDeadlineTicks = Stopwatch.GetTimestamp()
            + (long)Math.Max(1, durationSeconds) * Stopwatch.Frequency;
        using var poller = new Poller();
        var events = new PollEvent[activeClients.Count];
        for (int i = 0; i < activeClients.Count; i++)
            poller.Add(activeClients[i], PollEventFlags.PollIn, i);

        bool phaseDone = false;
        while (!phaseDone)
        {
            int readyCount = WaitForReadReady(poller, events);

            for (int readyOffset = 0; readyOffset < readyCount; readyOffset++)
            {
                if (events[readyOffset].Tag is not int i
                    || i < 0
                    || i >= activeClients.Count
                    || (events[readyOffset].Revents & PollEventFlags.PollIn) == 0)
                {
                    continue;
                }

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
                            phaseDone = true;
                            continue;
                        }

                        long recvTicks = Stopwatch.GetTimestamp();
                        if (recvTicks > benchDeadlineTicks)
                            continue;

                        bool headerOk = PerfRunner.TryDecodeMetricHeader(body,
                            out PerfMetricHeader header);
                        if (!headerOk
                            || header.RunId != expectedRunId
                            || header.MsgSize != (uint)msgSize)
                        {
                            continue;
                        }

                        if (header.Phase == (uint)PerfPhase.Active)
                        {
                            measureCount++;
                            if (header.SentTsNs > 0)
                            {
                                AddLatencySample(activeLatSamples,
                                    ref activeSampleSeen, latencySampleCap,
                                    ref rng, header);
                            }
                        }
                        else if (header.Phase == (uint)PerfPhase.Cooldown)
                        {
                            phaseDone = true;
                        }
                    }
                }
            }
        }

        double configuredSeconds = Math.Max(1.0, durationSeconds);
        double throughput = measureCount / configuredSeconds;
        double fallbackLatencyNs = (configuredSeconds * 1_000_000_000.0)
            / Math.Max(1.0, measureCount);
        var latency = ComputeLatencyStats(activeLatSamples);
        double latencyNs = latency.mean > 0.0 ? latency.mean : fallbackLatencyNs;
        double latencyP95Ns = latency.p95 > 0.0 ? latency.p95 : latencyNs;
        double latencyP99Ns = latency.p99 > 0.0 ? latency.p99 : latencyP95Ns;

        return (throughput, latencyNs, latencyP95Ns, latencyP99Ns, measureCount);
    }

    private static int WaitForReadReady(Poller poller, PollEvent[] events)
    {
        while (true)
        {
            int written = poller.Wait(events,
                TimeSpan.FromMilliseconds(-1), out _);
            if (written > 0)
                return written;
        }
    }

    private static void AddLatencySample(List<double> samples,
        ref long sampleSeen, int latencySampleCap, ref uint rng,
        PerfMetricHeader header)
    {
        if (header.SentTsNs == 0)
            return;
        ulong nowNs = EpochNs();
        if (nowNs < header.SentTsNs)
            return;
        ReservoirSample(samples, nowNs - header.SentTsNs, ref sampleSeen,
            latencySampleCap, ref rng);
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
