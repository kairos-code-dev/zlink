using System;
using System.Diagnostics;
using Systems.Zlink;
using static PerfRunner;

internal static class PerfMultiPubSubServer
{
    internal static int Run(PerfOptions options)
    {
        int size = Math.Max(1, options.Size);
        int sndTimeoutMs = ResolveMultiSndTimeoutMs(options);
        int readyTimeoutMs = ResolveMultiConnectReadyTimeoutMs(options);
        int clientCount = ResolveMultiClients(options);
        int durationSeconds = ResolveMultiDurationSeconds(options);
        int pollTimeoutMs = ResolveMultiClientPollTimeoutMs(options);
        string endpoint = MultiEndpointFor(options.Transport, "multi-pubsub",
            options);

        using var ctx = new Context();
        using var pollManager = new PollManager();
        using var controlState = new RunnerControlState(size);
        ApplyMultiServerContextOptions(ctx, options);
        using var server = new PubSocket(ctx);
        ApplyMultiSocketOptions(server, options);
        ConfigureTlsServerIfNeeded(server, options.Transport);
        server.Options.SendTimeout = TimeSpan.FromMilliseconds(sndTimeoutMs);
        server.Options.NoDrop = options.PubSubXpubNoDrop > 0;
        // Linger > 0 so wire-level stop tokens queued just before close
        // reach subscribers (default Linger=0 would drop them).
        server.Options.Linger = TimeSpan.FromSeconds(2);

        using var monitor = server.MonitorOpen(SocketEvent.ConnectionReady);

        server.Bind(endpoint);
        ApplyAutoHwmMsgUnit(server, size);
        RecalculateAutoHwm(ctx);
        WriteStdoutLine($"READY,{endpoint}");

        if (!WaitConnectReadyCount(monitor, clientCount, readyTimeoutMs))
            return 2;
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
        var pollSockets = new[] { server };

        if (!RunPublishPhase(pollManager, server, pollSockets, payload, runId,
                size, ref seq, durationSeconds, pollTimeoutMs, controlState))
        {
            return 2;
        }

        return 0;
    }

    private static bool PublishNoWait(PubSocket server, ReadOnlySpan<byte> payload)
    {
        try
        {
            using Message message = Message.FromBytes(payload);
            return server.Publish(string.Empty, message, SendFlags.DontWait);
        }
        catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                        || IsInterrupted(ex.InternalErrno))
        {
            return false;
        }
    }

    private static bool RunPublishPhase(PollManager pollManager, PubSocket server,
        IReadOnlyList<SocketBase> pollSockets, byte[] payload, uint runId,
        int size, ref ulong seq, int durationSeconds, int pollTimeoutMs,
        RunnerControlState controlState)
    {
        _ = pollManager;
        _ = pollSockets;
        _ = pollTimeoutMs;
        long deadlineTicks = Stopwatch.GetTimestamp()
            + (long)Math.Max(1, durationSeconds) * Stopwatch.Frequency;

        // PERF_MULTI_TEST_POLICY § 1.3.1: send Active payloads until the
        // deadline, then signal phase end via wire-level stop token.
        // Hot-spin DontWait sends — XPub no_drop=true may withhold POLLOUT
        // when any subscriber is briefly slow, so a -1 POLLOUT wait could
        // deadlock against the deadline check. The deadline is enforced
        // by the application clock at the loop head (no poller timer
        // fallback).
        while (!controlState.StopRequested
               && Stopwatch.GetTimestamp() < deadlineTicks)
        {
            StampMetricHeader(payload.AsSpan(), runId, PerfPhase.Active,
                size, seq, EpochNs());
            if (PublishNoWait(server, payload.AsSpan()))
            {
                seq++;
                continue;
            }

            // EAGAIN with no_drop XPub: yield and retry without a
            // poller timer wait.
            System.Threading.Thread.Yield();
        }

        if (!controlState.StopRequested)
            TrySendStopToken(server);

        return true;
    }

    private static void TrySendStopToken(PubSocket server)
    {
        for (int retry = 0; retry < 100; retry++)
        {
            try
            {
                using Message stopMessage = Message.FromBytes(MultiStopToken);
                if (server.Publish(string.Empty, stopMessage, SendFlags.DontWait))
                    return;
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                            || IsInterrupted(ex.InternalErrno))
            {
                System.Threading.Thread.Sleep(1);
                continue;
            }
            catch (ObjectDisposedException)
            {
                return;
            }

            System.Threading.Thread.Sleep(1);
        }
    }

}
