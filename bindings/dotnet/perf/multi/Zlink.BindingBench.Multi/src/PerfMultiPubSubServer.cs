using System;
using System.Diagnostics;
using Systems.Zlink;
using static PerfRunner;

internal static class PerfMultiPubSubServer
{
    private const string Topic = "bench";
    private const int PublishSocketTag = 0;
    private const int ActiveDeadlineTag = -1;
    private const int PublishRetryTag = -2;

    internal static int Run(PerfOptions options)
    {
        int size = Math.Max(1, options.Size);
        int sndTimeoutMs = ResolveMultiSndTimeoutMs(options);
        int readyTimeoutMs = ResolveMultiConnectReadyTimeoutMs(options);
        int clientCount = ResolveMultiClients(options);
        int durationSeconds = ResolveMultiDurationSeconds(options);
        string endpoint = MultiEndpointFor(options.Transport, "multi-pubsub",
            options);

        using var ctx = new Context();
        using var controlState = new RunnerControlState(size);
        ApplyMultiServerContextOptions(ctx, options);
        using var server = new PubSocket(ctx);
        ApplyMultiSocketOptions(server, options);
        ConfigureTlsServerIfNeeded(server, options.Transport);
        server.Options.SendTimeout = TimeSpan.FromMilliseconds(sndTimeoutMs);
        server.Options.NoDrop = options.PubSubXpubNoDrop > 0;
        server.Options.Linger = TimeSpan.Zero;

        server.Bind(endpoint);
        ApplyAutoHwmMsgUnit(server, size);
        RecalculateAutoHwm(ctx);
        PrintAutoHwmSnapshot(server, "server", options.Transport, size);
        WriteStdoutLine($"READY,{endpoint}");

        if (!controlState.WaitForStart(readyTimeoutMs))
        {
            if (!controlState.StopRequested)
                Console.Error.WriteLine("multi_server_error:no_start");
            return controlState.StopRequested ? 0 : 2;
        }

        const uint runId = 1;
        ulong seq = 1;
        var payload = new byte[Math.Max(size, PerfMetricHeaderSize)];
        Array.Fill(payload, (byte)'a');

        if (!RunPublishPhase(server, payload, runId, size, ref seq,
                PerfPhase.Active, durationSeconds, controlState)
            || !PublishStopToken(server, controlState))
        {
            return 2;
        }

        return 0;
    }

    private static bool PublishNoWait(PubSocket server, ReadOnlySpan<byte> payload)
    {
        try
        {
            using Message message = new(payload);
            return server.Publish(Topic).Message(message)
                .Flags(SendFlags.DontWait).Submit();
        }
        catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                        || IsInterrupted(ex.InternalErrno))
        {
            return false;
        }
    }

    private static bool PublishStopToken(PubSocket server,
        RunnerControlState controlState)
    {
        while (!controlState.StopRequested)
        {
            try
            {
                using Message message = new(MultiStopToken.AsSpan());
                if (server.Publish(Topic).Message(message)
                        .Flags(SendFlags.None).Submit())
                    return true;
            }
            catch (ZlinkException ex) when (IsTransientStopPublishErrno(
                                                ex.InternalErrno))
            {
            }
        }

        return controlState.StopRequested;
    }

    private static bool IsTransientStopPublishErrno(int errno)
    {
        return IsWouldBlock(errno) || IsInterrupted(errno) || errno == 110;
    }

    private static bool RunPublishPhase(PubSocket server, byte[] payload,
        uint runId, int size, ref ulong seq, PerfPhase phase, int durationSeconds,
        RunnerControlState controlState)
    {
        long deadlineTicks = Stopwatch.GetTimestamp()
            + (long)Math.Max(1, durationSeconds) * Stopwatch.Frequency;
        using var activeTimer = new Systems.Zlink.Timer();
        using var retryTimer = new Systems.Zlink.Timer();
        using var publishPoller = new Poller();
        var publishEvents = new PollEvent[3];
        publishPoller.Add(server, PollEventFlags.PollOut, PublishSocketTag);
        publishPoller.Add(activeTimer, ActiveDeadlineTag);
        publishPoller.Add(retryTimer, PublishRetryTag);
        activeTimer.Start(TimeSpan.FromSeconds(Math.Max(1, durationSeconds)),
            1);
        retryTimer.Start(TimeSpan.FromMilliseconds(10), 1);

        while (!controlState.StopRequested
               && Stopwatch.GetTimestamp() < deadlineTicks)
        {
            StampMetricHeader(payload.AsSpan(), runId, phase, size, seq,
                EpochNs());
            if (PublishNoWait(server, payload.AsSpan()))
            {
                seq++;
                continue;
            }

            if (!WaitForPublishReady(publishPoller, publishEvents,
                    retryTimer))
                break;
        }

        return true;
    }

    private static bool WaitForPublishReady(Poller poller, PollEvent[] events,
        Systems.Zlink.Timer retryTimer)
    {
        while (true)
        {
            try
            {
                int written = poller.Wait(events,
                    TimeSpan.FromMilliseconds(MultiClientPollTimeoutMs),
                    out _);
                for (int i = 0; i < written; i++)
                {
                    if (events[i].Tag is ActiveDeadlineTag)
                    {
                        _ = events[i].Timer?.Recv();
                        return false;
                    }

                    if (events[i].Tag is PublishRetryTag)
                    {
                        _ = events[i].Timer?.Recv();
                        retryTimer.Start(TimeSpan.FromMilliseconds(10), 1);
                        return true;
                    }

                    if (events[i].Tag is PublishSocketTag
                        && (events[i].Revents & PollEventFlags.PollOut) != 0)
                    {
                        return true;
                    }
                }
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                            || IsInterrupted(ex.InternalErrno))
            {
            }
        }
    }

}
