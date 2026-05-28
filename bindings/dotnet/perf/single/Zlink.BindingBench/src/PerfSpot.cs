using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Systems.Zlink;
using static PerfRunner;

internal static class PerfSpot
{
    private const string Topic = "bench";
    private const uint RunId = 1;
    private const uint ReadyPhase = 0;
    private const uint ActivePhase = 1;
    private const int SpotSocketTag = 0;
    private const int ReadyDeadlineTag = int.MaxValue;
    private const int ReadyRetryTag = int.MaxValue - 1;
    private static readonly RoutingId PubNodeRoutingId =
        RoutingId.From("z-perf-spot-pub"u8);
    private static readonly RoutingId SubNodeRoutingId =
        RoutingId.From("a-perf-spot-sub"u8);
    private static readonly RoutingId PubSpotRoutingId =
        RoutingId.From("z-perf-spot-pub-s"u8);
    private static readonly RoutingId SubSpotRoutingId =
        RoutingId.From("a-perf-spot-sub-s"u8);
    private static readonly RoutingId StopSpotRoutingId =
        RoutingId.From("a-perf-spot-stop-s"u8);

    internal static int RunSpot(string transport, int size)
    {
        if (!IsSupportedTransport(transport))
        {
            PrintUnsupported("SPOT", transport, size,
                "spot_transport_not_supported");
            return 0;
        }

        try
        {
            int durationSeconds = ResolveSingleDurationSeconds();
            int recvTimeoutMs = ResolveSingleRcvTimeoutMs();
            int readySettleMs = ResolveSingleSpotReadySettleMs();
            int latencySampleCap = ResolveSingleLatencyCount("SPOT");

            using var ctx = Zlink.CreateContext();
            ApplySingleContextOptions(ctx);
            ApplySingleAutoHwmMsgUnit(ctx, size);
            RecalculateSingleAutoHwm(ctx);
            using var pubNode = ctx.CreateSpotNode();
            using var subNode = ctx.CreateSpotNode();
            using var spotPub = pubNode.CreateSpot();
            using var spotSub = subNode.CreateSpot();
            // PERF_SINGLE_TEST_POLICY § 1.4: the SPOT one-way stop token is
            // published from a dedicated stop publisher on the subscriber
            // node so the terminator is not queued behind the saturated
            // remote data path (matches C bindings/c/perf/single/src/perf_spot.cpp).
            using var spotStop = subNode.CreateSpot();

            pubNode.SetRoutingId(PubNodeRoutingId);
            subNode.SetRoutingId(SubNodeRoutingId);
            spotPub.SetRoutingId(PubSpotRoutingId);
            spotSub.SetRoutingId(SubSpotRoutingId);
            spotStop.SetRoutingId(StopSpotRoutingId);
            ConfigureSpotNodeTlsIfNeeded(pubNode, transport);
            ConfigureSpotNodeTlsIfNeeded(subNode, transport);

            string publisherEndpoint = EndpointFor(transport, "spot-publisher");
            pubNode.SetPubBind(publisherEndpoint);
            publisherEndpoint = pubNode.LastEndpoint;
            subNode.ConnectPeer(publisherEndpoint);
            spotSub.SetSubscription(Topic);

            int payloadSize = Math.Max(size, PerfMetricHeaderSize);
            var probePayload = new byte[payloadSize];
            var activePayload = new byte[payloadSize];
            Array.Fill(probePayload, (byte)'p');
            Array.Fill(activePayload, (byte)'a');

            if (!WaitForSubscriptionReady(spotPub, spotSub, probePayload, size))
            {
                Console.Error.WriteLine("single_spot_error:subscription_not_ready");
                return 2;
            }

            if (readySettleMs > 0)
                Thread.Sleep(readySettleMs);

            if (!RunActiveWindow(spotPub, spotSub, spotStop, activePayload, size,
                    durationSeconds, recvTimeoutMs, latencySampleCap,
                    out long received, out List<double> latencySamples))
            {
                Console.Error.WriteLine("single_spot_error:active_window_failed");
                return 2;
            }

            if (received <= 0 || latencySamples.Count == 0)
                return 2;

            // ITEM 1: byte-identical SPOT AUTO_HWM_DETAIL emission (C parity:
            // perf_spot.cpp emits publisher then subscriber node sockets).
            EmitSpotAutoHwmDetail(pubNode, "publisher", transport, size);
            EmitSpotAutoHwmDetail(subNode, "subscriber", transport, size);

            double throughput = received / (double)Math.Max(durationSeconds, 1);
            var latency = ComputeLatencyStats(latencySamples);
            PrintResult("SPOT", transport, size, throughput, latency.mean,
                latency.p95, latency.p99);
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine(
                $"single_spot_error:exception:{ex.GetType().Name}:{ex.Message}");
            return 2;
        }
    }

    private static bool WaitForSubscriptionReady(ISpot publisher,
        ISpot subscriber, byte[] probePayload, int msgSize)
    {
        ulong seq = 1;
        if (!PublishMetricPayload(publisher, probePayload, msgSize, seq++,
                ReadyPhase, out _))
            return false;

        int readyTimeoutMs = ResolveSpotReadyTimeoutMs();
        using var deadlineTimer = Zlink.CreateTimer();
        using var retryTimer = Zlink.CreateTimer();
        using var poller = Zlink.CreatePoller();
        var events = new PollEvent[3];
        poller.Add(subscriber, PollEventFlags.PollIn, SpotSocketTag);
        poller.Add(deadlineTimer, ReadyDeadlineTag);
        poller.Add(retryTimer, ReadyRetryTag);
        deadlineTimer.Start(TimeSpan.FromMilliseconds(readyTimeoutMs), 1);
        retryTimer.Start(TimeSpan.FromMilliseconds(50), 1);
        using var subscribed = new TopicMessage();

        while (true)
        {
            while (true)
            {
                int recvRc = ReceiveSpotHeader(subscriber, msgSize,
                    subscribed, out var header, out bool headerOk);
                if (recvRc > 0)
                {
                    if (headerOk && IsExpectedSingleHeader(header, msgSize,
                            ReadyPhase, RunId))
                        return true;
                    continue;
                }

                if (recvRc < 0)
                    return false;

                break;
            }

            if (!WaitForReadyProbeEvent(poller, events, publisher,
                    probePayload, msgSize, ref seq, deadlineTimer, retryTimer))
                return false;
        }
    }

    private static bool RunActiveWindow(ISpot publisher, ISpot subscriber,
        ISpot stopPublisher, byte[] payload, int msgSize, int durationSeconds,
        int recvTimeoutMs, int latencySampleCap, out long received,
        out List<double> latencySamples)
    {
        _ = recvTimeoutMs;
        long activeDeadlineTicks = DeadlineTicksFromSeconds(
            Math.Max(1, durationSeconds));
        var samples = new List<double>(Math.Max(0, latencySampleCap));
        long activeReceived = 0;
        long sampleSeen = 0;
        uint rng = 0xA341316Cu;
        int ok = 1;

        // PERF_SINGLE_TEST_POLICY § 1.4: receiver exits on wire-level
        // stop token instead of `senderDone` + drain timer.
        var receiverThread = new Thread(() =>
        {
            // Reuse one TopicMessage envelope for the whole phase (parity
            // with C bindings/c/perf/single/src/perf_spot.cpp which reuses a
            // single stack header buffer). A fresh envelope per received
            // message churned the managed heap on the hot receiver thread;
            // the resulting GC activity stalled the drain loop and let the
            // multi-hop send/wire pipeline back up, inflating one-way
            // latency 100x-1000x vs C while throughput stayed comparable.
            using var subscribed = new TopicMessage();
            while (true)
            {
                int recvRc = ReceiveSpotPayload(subscriber, msgSize,
                    subscribed, out PerfMetricHeader header,
                    out bool headerOk, out bool isStopToken);
                if (recvRc > 0)
                {
                    if (isStopToken)
                        return;

                    long nowTicks = Stopwatch.GetTimestamp();
                    if (headerOk && IsExpectedSingleHeader(header, msgSize,
                            ActivePhase, RunId)
                        && nowTicks < activeDeadlineTicks)
                    {
                        Interlocked.Increment(ref activeReceived);
                        ulong nowNs = EpochNs();
                        if (nowNs >= header.SentTsNs)
                        {
                            ReservoirSample(samples, nowNs - header.SentTsNs,
                                ref sampleSeen, latencySampleCap, ref rng);
                        }
                    }

                    continue;
                }

                if (recvRc == 0)
                {
                    // Parity with C bindings/c/perf/single/src/perf_spot.cpp
                    // run_active_window: the SPOT subscriber handle has no
                    // poller-compatible FD, so C spins DONTWAIT recv with
                    // std::this_thread::yield() on empty. Parking on
                    // poller.Wait(-1) coalesces wakeups and lets the
                    // multi-hop send/wire pipeline accumulate a large
                    // standing in-flight set at equal throughput, which
                    // inflates measured one-way latency 100x-1000x vs C.
                    Thread.Yield();
                    continue;
                }

                Volatile.Write(ref ok, 0);
                return;
            }
        })
        {
            IsBackground = true
        };

        receiverThread.Start();

        ulong seq = 1;
        while (Stopwatch.GetTimestamp() < activeDeadlineTicks)
        {
            if (!PublishMetricPayload(publisher, payload, msgSize, seq,
                    ActivePhase, out bool sent))
            {
                Volatile.Write(ref ok, 0);
                break;
            }

            if (sent)
            {
                seq++;
            }
            else
            {
                // Parity with C bindings/c/perf/single/src/perf_spot.cpp
                // send_spot_samples: on transient backpressure C calls
                // perf_socket_poll(NULL, 0, 1) == ::poll(NULL, 0, 1), a 1ms
                // idle wait, instead of busy-republishing. Without this the
                // dotnet sender floods the multi-hop pipeline far past C's
                // working set, so each delivered message carries a much
                // older timestamp -> measured latency explodes while
                // throughput stays comparable.
                Thread.Sleep(1);
            }
        }

        // PERF_SINGLE_TEST_POLICY § 1.4: signal phase end via wire-level
        // stop token published from the dedicated stop publisher on the
        // subscriber node so it is not stuck behind the active backlog.
        PublishSpotStopTokenBlocking(stopPublisher, Topic, "[single-spot]");
        receiverThread.Join();

        received = activeReceived;
        latencySamples = samples;
        return Volatile.Read(ref ok) != 0;
    }

    private static bool WaitForReadyProbeEvent(IPoller poller,
        PollEvent[] events, ISpot publisher, byte[] probePayload, int msgSize,
        ref ulong seq, Systems.Zlink.IZlinkTimer deadlineTimer,
        Systems.Zlink.IZlinkTimer retryTimer)
    {
        while (true)
        {
            int written = poller.Wait(events, TimeSpan.FromMilliseconds(-1));
            for (int i = 0; i < written; i++)
            {
                if (events[i].Slot == (nuint)ReadyDeadlineTag)
                {
                    _ = deadlineTimer.Recv();
                    return false;
                }

                if (events[i].Slot == (nuint)ReadyRetryTag)
                {
                    _ = retryTimer.Recv();
                    if (!PublishMetricPayload(publisher, probePayload, msgSize,
                            seq++, ReadyPhase, out _))
                        return false;
                    retryTimer.Start(TimeSpan.FromMilliseconds(50), 1);
                    continue;
                }

                if (events[i].Slot == (nuint)SpotSocketTag
                    && (events[i].Revents & PollEventFlags.PollIn) != 0)
                    return true;
            }
        }
    }

    private static bool PublishMetricPayload(ISpot publisher, byte[] payload,
        int msgSize, ulong seq, uint phase, out bool sent)
    {
        sent = false;
        StampMetricHeader(payload.AsSpan(), RunId, phase, msgSize, seq,
            EpochNs());
        using var message = new Message(payload.AsSpan());
        try
        {
            sent = publisher.Publish(Topic)
                .Message(message)
                .Flags(SendFlags.DontWait)
                .Submit();
            return true;
        }
        catch (ZlinkException ex) when (IsInterrupted(ex.InternalErrno)
                                        || IsWouldBlock(ex.InternalErrno)
                                        || PerfShared.IsTransientNetworkError(
                                            ex.InternalErrno))
        {
            return true;
        }
    }

    private static int ReceiveSpotHeader(ISpot subscriber, int msgSize,
        TopicMessage subscribed, out PerfMetricHeader header,
        out bool headerOk)
    {
        return ReceiveSpotPayload(subscriber, msgSize, subscribed,
            out header, out headerOk, out _);
    }

    private static int ReceiveSpotPayload(ISpot subscriber, int msgSize,
        TopicMessage subscribed, out PerfMetricHeader header,
        out bool headerOk, out bool isStopToken)
    {
        header = default;
        headerOk = false;
        isStopToken = false;
        try
        {
            if (!subscriber.Subscribe(subscribed, RecvFlags.DontWait))
                return 0;
            if (!subscribed.IsSinglePart || subscribed.Topic != Topic)
                return 1;
        }
        catch (ZlinkException ex) when (IsInterrupted(ex.InternalErrno)
                                        || IsWouldBlock(ex.InternalErrno))
        {
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[single-spot] receive failed: {ex.Message}");
            return -1;
        }

        ReadOnlySpan<byte> payload = subscribed.FirstPart().AsReadOnlySpan();
        if (StopToken.IsStopToken(payload))
        {
            isStopToken = true;
            return 1;
        }
        headerOk = TryDecodeMetricHeader(payload, out header)
            && header.MsgSize == (uint)msgSize;
        return 1;
    }

    private static bool IsSupportedTransport(string transport)
    {
        return string.Equals(transport, "tcp", StringComparison.OrdinalIgnoreCase)
            || string.Equals(transport, "tls", StringComparison.OrdinalIgnoreCase)
            || string.Equals(transport, "ws", StringComparison.OrdinalIgnoreCase)
            || string.Equals(transport, "wss", StringComparison.OrdinalIgnoreCase);
    }

    private static bool IsWouldBlock(int errno)
    {
        return PerfShared.IsWouldBlock(errno);
    }

    private static bool IsInterrupted(int errno)
    {
        return PerfShared.IsInterrupted(errno);
    }
}
