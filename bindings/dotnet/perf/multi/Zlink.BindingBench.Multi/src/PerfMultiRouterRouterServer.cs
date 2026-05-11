using System;
using System.Collections.Generic;
using Systems.Zlink;
using static PerfRunner;

internal static class PerfMultiRouterRouterServer
{
    internal static int Run(PerfOptions options)
    {
        int size = Math.Max(1, options.Size);
        int rcvTimeoutMs = ResolveMultiRcvTimeoutMs(options);
        int readyTimeoutMs = ResolveMultiConnectReadyTimeoutMs(options);
        int clientCount = ResolveMultiClients(options);
        int pollTimeoutMs = ResolveMultiClientPollTimeoutMs(options);
        string endpoint = MultiEndpointFor(options.Transport,
            "multi-router-router", options);

        using var ctx = new Context();
        using var pollManager = new PollManager();
        ApplyMultiServerContextOptions(ctx, options);
        using var server = new RouterSocket(ctx);
        ApplyMultiSocketOptions(server, options);
        ConfigureTlsServerIfNeeded(server, options.Transport);
        server.SetRoutingId(RoutingId.FromBytes("SERVER"u8));
        using var monitor = server.MonitorOpen(SocketEvent.ConnectionReady);

        server.Options.ReceiveTimeout = TimeSpan.FromMilliseconds(rcvTimeoutMs);
        server.Bind(endpoint);
        WriteStdoutLine($"READY,{endpoint}");

        if (!WaitConnectReadyCount(monitor, clientCount, readyTimeoutMs))
            return 2;

        ApplyAutoHwmMsgUnit(server, size);
        RecalculateAutoHwm(ctx);

        var sockets = new[] { (SocketBase)server };
        var eventMasks = new[] { SocketPollIn };
        var pendingReplies = new Queue<PendingReply>();

        bool stop = false;
        long pollCount = 0;
        long drainCount = 0;
        long sendCount = 0;
        // Diagnostic: when PERF_DOTNET_TIMING=1 is set, accumulate elapsed
        // ticks across the inner drain steps to identify the real hot
        // stack without external profilers. Each Stopwatch.GetTimestamp
        // call is ~20 ns so the overhead is a flat tax; ratios between
        // segments are still informative.
        bool timing = Environment.GetEnvironmentVariable(
            "PERF_DOTNET_TIMING") == "1";
        long recvTicks = 0;
        long bodyTicks = 0;
        long sendTicks = 0;
        long disposeTicks = 0;
        while (!stop)
        {
            eventMasks[0] = pendingReplies.Count > 0
                ? SocketPollIn | SocketPollOut
                : SocketPollIn;
            pollCount++;
            if (PollSocketEvents(pollManager, sockets, eventMasks,
                    pollTimeoutMs) <= 0)
            {
                continue;
            }

            if (IsSocketWriteReady(pollManager, 0)
                && !FlushPendingReplies(server, pendingReplies))
            {
                return 2;
            }

            if (!IsSocketReadReady(pollManager, 0))
                continue;

            while (true)
            {
                long t0 = timing ? System.Diagnostics.Stopwatch.GetTimestamp() : 0;
                Received? received = TryRecvNoWait(server);
                long t1 = timing ? System.Diagnostics.Stopwatch.GetTimestamp() : 0;
                if (received == null)
                {
                    if (timing) recvTicks += t1 - t0;
                    break;
                }
                drainCount++;

                Message bodyMessage = received.SinglePartOrThrow();
                ReadOnlySpan<byte> body = bodyMessage.AsReadOnlySpan();
                if (IsStopTokenPayload(body))
                {
                    received.Dispose();
                    stop = true;
                    break;
                }
                long t2 = timing ? System.Diagnostics.Stopwatch.GetTimestamp() : 0;

                if (received.RoutingId == null)
                {
                    received.Dispose();
                    return 2;
                }
                RoutingId routingId = received.RoutingId.Value;

                if (pendingReplies.Count == 0
                    && server.Send(routingId, bodyMessage, SendFlags.DontWait))
                {
                    sendCount++;
                    long t3 = timing ? System.Diagnostics.Stopwatch.GetTimestamp() : 0;
                    received.Dispose();
                    long t4 = timing ? System.Diagnostics.Stopwatch.GetTimestamp() : 0;
                    if (timing)
                    {
                        recvTicks += t1 - t0;
                        bodyTicks += t2 - t1;
                        sendTicks += t3 - t2;
                        disposeTicks += t4 - t3;
                    }
                    continue;
                }

                Message reply = bodyMessage.Move();
                if (!EnqueueReplyOrSend(server, pendingReplies, routingId,
                        reply))
                {
                    reply.Dispose();
                    received.Dispose();
                    return 2;
                }
                received.Dispose();
            }
        }

        if (Environment.GetEnvironmentVariable("PERF_DOTNET_SERVER_STATS")
            == "1")
        {
            Console.Error.WriteLine(
                $"[dotnet-server-stats] polls={pollCount} drains={drainCount} sends={sendCount} drainsPerPoll={(pollCount > 0 ? (double)drainCount / pollCount : 0):F2}");
        }
        if (timing && drainCount > 0)
        {
            double tickPerNs = 1_000_000_000.0
                / System.Diagnostics.Stopwatch.Frequency;
            Console.Error.WriteLine(
                $"[dotnet-server-timing] per-drain ns: " +
                $"recv={recvTicks * tickPerNs / drainCount:F0} " +
                $"body={bodyTicks * tickPerNs / drainCount:F0} " +
                $"send={sendTicks * tickPerNs / drainCount:F0} " +
                $"dispose={disposeTicks * tickPerNs / drainCount:F0} " +
                $"total={(recvTicks + bodyTicks + sendTicks + disposeTicks) * tickPerNs / drainCount:F0}");
        }
        return 0;
    }

    private static bool EnqueueReplyOrSend(RouterSocket server,
        Queue<PendingReply> pendingReplies, RoutingId routingId, Message reply)
    {
        if (pendingReplies.Count == 0)
        {
            if (server.Send(routingId, reply, SendFlags.DontWait))
            {
                reply.Dispose();
                return true;
            }
        }

        pendingReplies.Enqueue(new PendingReply(routingId, reply));
        return true;
    }

    private static bool FlushPendingReplies(RouterSocket server,
        Queue<PendingReply> pendingReplies)
    {
        while (pendingReplies.Count > 0)
        {
            PendingReply pending = pendingReplies.Peek();
            if (server.Send(pending.RoutingId, pending.Message,
                    SendFlags.DontWait))
            {
                pendingReplies.Dequeue();
                pending.Dispose();
                continue;
            }

            return true;
        }

        return true;
    }

    private static Received? TryRecvNoWait(RouterSocket socket)
    {
        return socket.Recv(RecvFlags.DontWait);
    }

    private sealed class PendingReply : IDisposable
    {
        internal PendingReply(RoutingId routingId, Message message)
        {
            RoutingId = routingId;
            Message = message;
        }

        internal RoutingId RoutingId { get; }
        internal Message Message { get; }

        public void Dispose()
        {
            Message.Dispose();
        }
    }
}
