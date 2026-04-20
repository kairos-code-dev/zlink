using System;
using System.Diagnostics;
using Zlink;
using static PerfRunner;

internal static class PerfMultiPubSubServer
{
    internal static int Run(PerfOptions options)
    {
        int size = Math.Max(1, options.Size);
        int durationSeconds = ResolveMultiDurationSeconds(options);
        int sndTimeoutMs = ResolveMultiSndTimeoutMs(options);
        int readyTimeoutMs = ResolveMultiConnectReadyTimeoutMs(options);
        int clientCount = ResolveMultiClients(options);
        string endpoint = MultiEndpointFor(options.Transport, "multi-pubsub",
            options);

        using var ctx = new Context();
        using var pollManager = new PollManager();
        ApplyMultiServerContextOptions(ctx, options);
        using var server = new PubSocket(ctx);
        ApplyMultiSocketOptions(server, options);
        ConfigureTlsServerIfNeeded(server, options.Transport);
        server.SetOption(SocketOptions.SndTimeo, sndTimeoutMs);
        server.SetOption(SocketOptions.XPubNoDrop,
            options.PubSubXpubNoDrop > 0 ? 1 : 0);

        using var monitor = server.MonitorOpen(SocketEvent.ConnectionReady);

        server.Bind(endpoint);
        WriteStdoutLine($"READY,{endpoint}");

        if (!WaitConnectReadyCount(monitor, clientCount, readyTimeoutMs))
            return 2;

        const uint runId = 1;
        ulong seq = 1;
        var payload = new byte[Math.Max(size, PerfMetricHeaderSize)];
        Array.Fill(payload, (byte)'a');
        var pollSockets = new[] { server };

        long benchDeadlineTicks = Stopwatch.GetTimestamp()
            + (long)Math.Max(1, durationSeconds) * Stopwatch.Frequency;
        long activeSent = RunPublishPhase(pollManager, server, pollSockets,
            payload, runId, size, PerfPhase.Active, ref seq, benchDeadlineTicks);

        if (activeSent > 0)
        {
            double configuredSeconds = Math.Max(1.0, durationSeconds);
            double throughput = activeSent / configuredSeconds;
            double latencyNs = (configuredSeconds * 1_000_000_000.0)
                / Math.Max(1.0, activeSent);
            PrintResult(options.Pattern, options.Transport, size, throughput, latencyNs,
                latencyNs, latencyNs);
        }

        return 0;
    }

    private static bool PublishNoWait(PubSocket server, ReadOnlySpan<byte> payload)
    {
        try
        {
            using var message = Message.FromBytes(payload);
            server.Publish(string.Empty, message, SendFlags.DontWait);
            return true;
        }
        catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                        || IsInterrupted(ex.InternalErrno))
        {
            return false;
        }
    }

    private static long RunPublishPhase(PollManager pollManager, PubSocket server,
        IReadOnlyList<SocketBase> pollSockets, byte[] payload, uint runId,
        int size, PerfPhase phase, ref ulong seq, long deadlineTicks)
    {
        bool sendPending = false;
        long sent = 0;

        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            bool trySend = !sendPending;
            if (!trySend)
            {
                int pollTimeoutMs = RemainingMilliseconds(deadlineTicks);
                if (PollSocketWriteReady(pollManager, pollSockets,
                        pollTimeoutMs) > 0
                    && IsSocketWriteReady(pollManager, 0))
                {
                    trySend = true;
                }
            }

            if (trySend)
            {
                StampMetricHeader(payload.AsSpan(), runId, phase, size, seq,
                    EpochNs());
                if (PublishNoWait(server, payload.AsSpan()))
                {
                    seq++;
                    sent++;
                    sendPending = false;
                    continue;
                }
                sendPending = true;
            }

            int timeoutMs = Math.Min(50, RemainingMilliseconds(deadlineTicks));
            if (timeoutMs <= 0)
                continue;

            if (sendPending)
            {
                _ = PollSocketWriteReady(pollManager, pollSockets, timeoutMs);
            }
            else
            {
                _ = PollSocketWriteReady(pollManager, pollSockets, timeoutMs);
            }
        }

        return sent;
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
