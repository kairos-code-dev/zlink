using System;
using System.Collections.Generic;
using Systems.Zlink;
using static PerfRunner;

internal static class PerfMultiDealerRouterServer
{

    internal static int Run(PerfOptions options)
    {
        int size = Math.Max(1, options.Size);
        int rcvTimeoutMs = ResolveMultiRcvTimeoutMs(options);
        int readyTimeoutMs = ResolveMultiConnectReadyTimeoutMs(options);
        int clientCount = ResolveMultiClients(options);
        int pollTimeoutMs = ResolveMultiClientPollTimeoutMs(options);
        string endpoint = MultiEndpointFor(options.Transport,
            "multi-dealer-router", options);

        using var ctx = new Context();
        using var pollManager = new PollManager();
        ApplyMultiServerContextOptions(ctx, options);
        using var server = new RouterSocket(ctx);
        ApplyMultiSocketOptions(server, options);
        ConfigureTlsServerIfNeeded(server, options.Transport);
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
        while (!stop)
        {
            eventMasks[0] = pendingReplies.Count > 0
                ? SocketPollIn | SocketPollOut
                : SocketPollIn;
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
                using Received? received = TryRecvNoWait(server);
                if (received == null)
                    break;

                // Skip Parts.Count: it materializes a
                // MultipartMessageCollection wrapper around the lazy
                // single-part message (one heap alloc per recv).
                Message bodyMessage = received.SinglePartOrThrow();
                ReadOnlySpan<byte> body = bodyMessage.AsReadOnlySpan();
                if (IsStopTokenPayload(body))
                {
                    stop = true;
                    break;
                }

                if (received.RoutingId == null)
                    return 2;
                RoutingId routingId = received.RoutingId.Value;

                if (pendingReplies.Count == 0
                    && server.Send(routingId, bodyMessage, SendFlags.DontWait))
                {
                    continue;
                }

                using Message reply = bodyMessage.Move();
                if (!EnqueueReplyOrSend(server, pendingReplies, routingId,
                        reply))
                {
                    return 2;
                }
            }
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
