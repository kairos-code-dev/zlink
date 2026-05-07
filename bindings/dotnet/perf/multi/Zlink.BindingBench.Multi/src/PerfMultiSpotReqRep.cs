using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text;
using System.Threading;
using Systems.Zlink;
using static PerfRunner;

internal static class PerfMultiSpotReqRep
{
    private const string ChannelName = "perf.spot.reqrep";
    private const uint RunId = 1;

    internal static int RunServer(PerfOptions options)
    {
        int size = Math.Max(1, options.Size);
        string bindEndpoint = MultiEndpointFor(options.Transport,
            "multi-spot-reqrep", options);

        using var ctx = new Context();
        ApplyMultiServerContextOptions(ctx, options);
        using var responder = new RouterSocket(ctx);
        try
        {
            ApplyMultiSocketOptions(responder, options);
            ConfigureTlsServerIfNeeded(responder, options.Transport);
            responder.Bind(bindEndpoint);
            WriteStdoutLine(
                $"READY,{NormalizeClientEndpoint(bindEndpoint, options.Transport)}");

            int stopRequested = 0;
            StartStopWatcher(() => Volatile.Write(ref stopRequested, 1));
            while (Volatile.Read(ref stopRequested) == 0)
            {
                using Received? received = responder.Recv(RecvFlags.DontWait);
                if (received == null)
                {
                    Thread.Sleep(1);
                    continue;
                }

                RoutingId routingId = received.RoutingId
                    ?? throw new InvalidOperationException("missing routing id");
                ulong requestSeq = received.RequestSeq ?? 0UL;
                using Message reply = received.FirstPart().Move();
                responder.Reply(routingId, requestSeq, reply);
            }

            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine(
                $"multi_server_error:{ex.GetType().Name}:{ex.Message}");
            return 2;
        }
    }

    private static void StartStopWatcher(Action requestStop)
    {
        var watcher = new Thread(static arg =>
        {
            var stop = (Action)arg!;
            while (true)
            {
                string? line = Console.ReadLine();
                if (line == null)
                {
                    stop();
                    return;
                }

                if (string.Equals(line, "STOP", StringComparison.OrdinalIgnoreCase)
                    || string.Equals(line, "QUIT", StringComparison.OrdinalIgnoreCase))
                {
                    stop();
                    return;
                }
            }
        })
        {
            IsBackground = true,
        };
        watcher.Start(requestStop);
    }

    internal static int RunClient(PerfOptions options)
    {
        int size = Math.Max(1, options.Size);
        int clientCount = ResolveMultiClients(options);
        int readyTimeoutMs = ResolveMultiConnectReadyTimeoutMs(options);
        int durationSeconds = ResolveMultiDurationSeconds(options);
        int latencySampleCap = ResolveMultiLatencySampleCap(options);
        string endpoint = NormalizeClientEndpoint(options.Endpoint, options.Transport);

        using var ctx = new Context();
        using var controlState = new RunnerControlState(size);
        ApplyMultiClientContextOptions(ctx, options);
        var slots = new List<ClientSlot>(clientCount);
        try
        {
            for (int i = 0; i < clientCount; i++)
            {
                var node = new SpotNode(ctx);
                var dealer = new DealerSocket(ctx);
                var requester = node.CreateSpot();
                ApplyMultiSocketOptions(dealer, options);
                ConfigureTlsClientIfNeeded(dealer, options.Transport);
                dealer.SetRoutingId(RoutingId.FromBytes(
                    Encoding.ASCII.GetBytes($"SPOT-REQREP-{i}")));
                node.AttachChannelDealerManual(ChannelName, dealer);
                dealer.Connect(endpoint);
                slots.Add(new ClientSlot(node, dealer, requester, latencySampleCap));
            }

            if (!WarmupClients(slots, size, readyTimeoutMs))
                return 2;

            WriteStdoutLine($"CLIENT_READY,{size}");
            if (!controlState.WaitForStart(readyTimeoutMs))
            {
                if (!controlState.StopRequested)
                    Console.Error.WriteLine("multi_client_error:no_start");
                return controlState.StopRequested ? 0 : 2;
            }

            if (!RunClientWorkers(slots, size, durationSeconds, readyTimeoutMs))
                return 2;

            return PrintMultiResult(options.Transport, size, durationSeconds,
                latencySampleCap, slots);
        }
        finally
        {
            foreach (ClientSlot slot in slots)
                slot.Dispose();
        }
    }

    private static bool WarmupClients(List<ClientSlot> slots, int size,
        int readyTimeoutMs)
    {
        Exception? failure = null;
        using var ready = new CountdownEvent(slots.Count);
        var threads = new List<Thread>(slots.Count);
        foreach (ClientSlot slot in slots)
        {
            var thread = new Thread(() =>
            {
                try
                {
                    long deadlineTicks = DeadlineTicksFromMilliseconds(readyTimeoutMs);
                    while (Stopwatch.GetTimestamp() < deadlineTicks)
                    {
                        try
                        {
                            using var probe = Message.FromBytes(CreatePayload(size,
                                (uint)PerfPhase.Warmup, 1));
                            PerfMetricHeader reply = RequestReply(slot.Requester,
                                size, probe, TimeSpan.FromMilliseconds(200));
                            if (reply.Phase == (uint)PerfPhase.Warmup)
                            {
                                ready.Signal();
                                return;
                            }
                        }
                        catch
                        {
                            Thread.Sleep(10);
                        }
                    }
                    throw new InvalidOperationException(
                        "spot reqrep warmup reply timed out");
                }
                catch (Exception ex)
                {
                    Interlocked.CompareExchange(ref failure, ex, null);
                    while (ready.CurrentCount > 0)
                    {
                        try
                        {
                            ready.Signal();
                        }
                        catch (InvalidOperationException)
                        {
                            break;
                        }
                    }
                }
            })
            {
                IsBackground = true,
            };
            threads.Add(thread);
            thread.Start();
        }

        ready.Wait(Math.Max(readyTimeoutMs, 1));
        foreach (Thread thread in threads)
            thread.Join();
        if (failure != null)
        {
            Console.Error.WriteLine(
                $"multi_client_error:{failure.GetType().Name}:{failure.Message}");
            return false;
        }
        return ready.CurrentCount == 0;
    }

    private static bool RunClientWorkers(List<ClientSlot> slots, int size,
        int durationSeconds, int readyTimeoutMs)
    {
        Exception? failure = null;
        var threads = new List<Thread>(slots.Count);
        long activeDeadlineTicks = DeadlineTicksFromSeconds(durationSeconds);
        for (int i = 0; i < slots.Count; i++)
        {
            ClientSlot slot = slots[i];
            var thread = new Thread(() =>
            {
                try
                {
                    ulong seq = 1;
                    while (Stopwatch.GetTimestamp() < activeDeadlineTicks)
                    {
                        using var active = Message.FromBytes(CreatePayload(size,
                            (uint)PerfPhase.Active, seq++));
                        PerfMetricHeader reply = RequestReply(slot.Requester, size,
                            active, TimeSpan.FromSeconds(2));
                        if (reply.Phase != (uint)PerfPhase.Active)
                            continue;

                        slot.MeasureCount++;
                        ulong nowNs = EpochNs();
                        if (nowNs >= reply.SentTsNs)
                            slot.RecordLatency((nowNs - reply.SentTsNs) / 2.0);
                    }

                    using var cooldown = Message.FromBytes(CreatePayload(size,
                        (uint)PerfPhase.Cooldown, seq));
                    _ = RequestReply(slot.Requester, size, cooldown,
                        TimeSpan.FromMilliseconds(Math.Max(1, readyTimeoutMs)));
                }
                catch (Exception ex)
                {
                    Interlocked.CompareExchange(ref failure, ex, null);
                }
            })
            {
                IsBackground = true,
            };
            threads.Add(thread);
            thread.Start();
        }

        foreach (Thread thread in threads)
            thread.Join();
        if (failure != null)
        {
            Console.Error.WriteLine(
                $"multi_client_error:{failure.GetType().Name}:{failure.Message}");
            return false;
        }
        return true;
    }

    private static int PrintMultiResult(string transport, int size,
        int durationSeconds, int latencySampleCap, List<ClientSlot> slots)
    {
        long measureCount = 0;
        var samples = new List<double>(Math.Max(0, latencySampleCap));
        long sampleSeen = 0;
        uint rng = 0xA341316Cu;
        foreach (ClientSlot slot in slots)
        {
            measureCount += slot.MeasureCount;
            foreach (double sample in slot.LatencySamples)
            {
                ReservoirSample(samples, sample, ref sampleSeen,
                    latencySampleCap, ref rng);
            }
        }

        if (measureCount <= 0)
            return 2;

        double activeSeconds = Math.Max(1.0, durationSeconds);
        double throughput = measureCount / activeSeconds;
        double fallbackLatencyNs = (activeSeconds * 1_000_000_000.0)
            / Math.Max(1.0, measureCount * 2.0);
        var latency = ComputeLatencyStats(samples);
        double latencyNs = latency.mean > 0.0 ? latency.mean : fallbackLatencyNs;
        double latencyP95Ns = latency.p95 > 0.0 ? latency.p95 : latencyNs;
        double latencyP99Ns = latency.p99 > 0.0 ? latency.p99 : latencyP95Ns;
        PrintResult("SPOT_REQREP", transport, size, throughput, latencyNs,
            latencyP95Ns, latencyP99Ns);
        return 0;
    }

    private static PerfMetricHeader RequestReply(Spot requester, int size,
        Message payload, TimeSpan timeout)
    {
        IReadOnlyList<Message> replyParts = requester.RequestChannelAsync(
                ChannelName, payload, timeout)
            .GetAwaiter().GetResult();
        try
        {
            if (replyParts.Count == 0)
                throw new InvalidOperationException("spot reqrep reply was empty");

            ReadOnlySpan<byte> body = replyParts[0].AsReadOnlySpan();
            if (PerfRunner.TryDecodeMetricHeader(body, out PerfMetricHeader header)
                && header.MsgSize == (uint)size
                && header.RunId == RunId)
            {
                return header;
            }

            throw new InvalidOperationException("spot reqrep reply header invalid");
        }
        finally
        {
            for (int i = 0; i < replyParts.Count; i++)
                replyParts[i].Dispose();
        }
    }

    private static byte[] CreatePayload(int size, uint phase, ulong seq)
    {
        var payload = new byte[Math.Max(size, PerfMetricHeaderSize)];
        Array.Fill(payload, (byte)'a');
        StampMetricHeader(payload.AsSpan(), RunId, (PerfPhase)phase, size, seq,
            EpochNs());
        return payload;
    }

    private static string NormalizeClientEndpoint(string endpoint, string transport)
    {
        if (!transport.Equals("tls", StringComparison.OrdinalIgnoreCase)
            && !transport.Equals("wss", StringComparison.OrdinalIgnoreCase))
        {
            return endpoint;
        }

        return endpoint.Replace("://127.0.0.1:", "://localhost:",
            StringComparison.Ordinal);
    }

    private sealed class ClientSlot : IDisposable
    {
        private readonly int _latencySampleCap;

        internal ClientSlot(SpotNode node, DealerSocket dealer, Spot requester,
            int latencySampleCap)
        {
            Node = node;
            Dealer = dealer;
            Requester = requester;
            _latencySampleCap = Math.Max(1, latencySampleCap);
        }

        internal SpotNode Node { get; }
        internal DealerSocket Dealer { get; }
        internal Spot Requester { get; }
        internal List<double> LatencySamples { get; } = new();
        internal long MeasureCount { get; set; }

        internal void RecordLatency(double latencyNs)
        {
            if (LatencySamples.Count < _latencySampleCap)
                LatencySamples.Add(latencyNs);
        }

        public void Dispose()
        {
            Requester.Dispose();
            Dealer.Dispose();
            Node.Dispose();
        }
    }
}
