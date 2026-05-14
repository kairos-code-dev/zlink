using System;
using System.Collections.Generic;
using System.Diagnostics;
using Systems.Zlink;
using static PerfRunner;

internal static class PerfMultiDealerDealerServer
{
    internal static int Run(PerfOptions options)
    {
        int size = Math.Max(1, options.Size);
        int sndTimeoutMs = ResolveMultiSndTimeoutMs(options);
        int readyTimeoutMs = ResolveMultiConnectReadyTimeoutMs(options);
        int clientCount = ResolveMultiClients(options);
        int durationSeconds = ResolveMultiDurationSeconds(options);
        int pollTimeoutMs = ResolveMultiClientPollTimeoutMs(options);
        string endpoint = MultiEndpointFor(options.Transport,
            "multi-dealer-dealer", options);

        using var ctx = new Context();
        using var pollManager = new PollManager();
        using var controlState = new RunnerControlState(size);
        ApplyMultiServerContextOptions(ctx, options);
        using var server = new DealerSocket(ctx);
        ApplyMultiSocketOptions(server, options);
        ConfigureTlsServerIfNeeded(server, options.Transport);
        // Linger > 0 so wire-level stop tokens queued just before
        // socket close are actually transmitted. Default Linger=0 from
        // ApplyMultiSocketOptions would drop pending sends on dispose.
        server.Options.Linger = TimeSpan.FromSeconds(2);
        using var monitor = server.MonitorOpen(SocketEvent.ConnectionReady);

        server.Options.SendTimeout = TimeSpan.FromMilliseconds(sndTimeoutMs);
        server.Bind(endpoint);
        WriteStdoutLine($"READY,{endpoint}");

        if (!WaitConnectReadyCount(monitor, clientCount, readyTimeoutMs))
            return 2;

        if (!controlState.WaitForStart(readyTimeoutMs))
        {
            if (!controlState.StopRequested)
                Console.Error.WriteLine("multi_server_error:no_start");
            return controlState.StopRequested ? 0 : 2;
        }

        ApplyAutoHwmMsgUnit(server, size);
        RecalculateAutoHwm(ctx);
        PrintAutoHwmSnapshot(server, "server", options.Transport, size);

        var sockets = new[] { (SocketBase)server };
        var payload = new byte[Math.Max(size, PerfMetricHeaderSize)];
        Array.Fill(payload, (byte)'a');

        return RunPublishPhase(pollManager, server, sockets, payload, size,
            durationSeconds, pollTimeoutMs, controlState, clientCount) ? 0 : 2;
    }

    private static bool RunPublishPhase(PollManager pollManager,
        DealerSocket server, IReadOnlyList<SocketBase> pollSockets,
        byte[] payload, int msgSize, int durationSeconds, int pollTimeoutMs,
        RunnerControlState controlState, int clientCount)
    {
        _ = pollManager;
        _ = pollSockets;
        _ = pollTimeoutMs;
        const uint runId = 1;
        ulong seq = 1;
        long activeDeadlineTicks = Stopwatch.GetTimestamp()
            + (long)Math.Max(1, durationSeconds) * Stopwatch.Frequency;

        // PERF_MULTI_TEST_POLICY § 1.3.1: send Active payloads until the
        // deadline, then signal phase end via wire-level stop token.
        // Hot-spin DontWait sends — a -1 POLLOUT wait could deadlock
        // against the deadline check when a peer's queue is briefly
        // full. The deadline is enforced by the application clock at
        // the loop head.
        while (!controlState.StopRequested
               && Stopwatch.GetTimestamp() < activeDeadlineTicks)
        {
            StampMetricHeader(payload.AsSpan(), runId, PerfPhase.Active,
                msgSize, seq, EpochNs());
            using Message message = new(payload.AsSpan());
            if (server.Send().Message(message).Flags(SendFlags.DontWait)
                .Submit())
            {
                seq++;
                continue;
            }

            System.Threading.Thread.Yield();
        }

        if (!controlState.StopRequested)
        {
            // DealerDealer fanout: round-robin to N peers means we must
            // emit N stop tokens to terminate every client receiver.
            int sentStops = 0;
            long stopDeadlineTicks = Stopwatch.GetTimestamp()
                + 30L * Stopwatch.Frequency;
            while (sentStops < clientCount
                   && Stopwatch.GetTimestamp() < stopDeadlineTicks)
            {
                if (TrySendStopToken(server))
                {
                    sentStops++;
                    continue;
                }

                System.Threading.Thread.Yield();
            }
        }

        return true;
    }

    private static bool TrySendStopToken(DealerSocket server)
    {
        try
        {
            using Message stopMessage = new(MultiStopToken.AsSpan());
            return server.Send().Message(stopMessage).Flags(SendFlags.DontWait)
                .Submit();
        }
        catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                        || IsInterrupted(ex.InternalErrno))
        {
            return false;
        }
        catch (ObjectDisposedException)
        {
            return false;
        }
    }

}
