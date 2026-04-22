using System;
using System.Collections.Generic;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfMultiStreamServer
{
    private const string Pattern = "STREAM";

    private enum SendStatus
    {
        Done = 0,
        Blocked = 1,
        Fatal = 2,
    }

    internal static int Run(PerfOptions options)
    {
        if (!IsCoreStreamServerTransport(options.Transport))
        {
            return PerfRunner.PrintUnsupported(Pattern, options.Transport,
                options.Size,
                "transport_not_supported");
        }

        int ioTimeoutMs = Math.Max(ResolveMultiSndTimeoutMs(options),
            ResolveMultiRcvTimeoutMs(options));
        int pendingCapacity = ResolvePendingCapacity(options);
        string endpoint = MultiEndpointFor(options.Transport, "multi-stream",
            options);

        using var ctx = new Context();
        ApplyMultiServerContextOptions(ctx, options);
        using var server = new StreamSocket(ctx);
        ApplyMultiSocketOptions(server, options);
        ConfigureTlsServerIfNeeded(server, options.Transport);
        server.Options.SendTimeout = TimeSpan.FromMilliseconds(ioTimeoutMs);
        server.Options.ReceiveTimeout = TimeSpan.FromMilliseconds(ioTimeoutMs);
        server.Options.TcpNoDelay = true;
        server.Bind(endpoint);
        WriteStdoutLine($"READY,{endpoint}");

        var pending = new Queue<PendingStreamMessage>(pendingCapacity);
        object pendingLock = new();
        using var pendingSignal = new ManualResetEventSlim(false);
        var control = new ControlState();
        StartControlWatcher(control);

        server.OnPacket((routingId, payload) =>
        {
            PendingStreamMessage request = new();
            request.Assign(routingId, payload);
            SendStatus sendStatus = TrySendPendingMessage(server, request);
            if (sendStatus == SendStatus.Done)
            {
                request.Clear();
                return 0;
            }

            if (sendStatus == SendStatus.Fatal)
            {
                request.Clear();
                Interlocked.Exchange(ref control.StopRequested, 1);
                return -1;
            }

            lock (pendingLock)
            {
                if (pending.Count >= pendingCapacity)
                {
                    request.Clear();
                    Interlocked.Exchange(ref control.StopRequested, 1);
                    return -1;
                }

                pending.Enqueue(request);
                pendingSignal.Set();
            }
            return 0;
        });

        int rc = 0;
        while (Volatile.Read(ref control.StopRequested) == 0)
        {
            if (!FlushPendingMessages(server, pending, pendingLock,
                    pendingSignal, ref rc))
            {
                break;
            }

            if (Volatile.Read(ref control.StopRequested) != 0)
                break;

            pendingSignal.Wait(50);
            pendingSignal.Reset();
        }

        return rc;
    }

    private static void StartControlWatcher(ControlState control)
    {
        Thread watcher = new(() =>
        {
            try
            {
                string? line;
                while ((line = Console.In.ReadLine()) != null)
                {
                    if (line == "STOP" || line == "QUIT")
                    {
                        Interlocked.Exchange(ref control.StopRequested, 1);
                        return;
                    }
                }
            }
            finally
            {
                Interlocked.Exchange(ref control.StopRequested, 1);
            }
        });
        watcher.IsBackground = true;
        watcher.Start();
    }

    private static int ResolvePendingCapacity(PerfOptions options)
    {
        int clients = options.Clients;
        int hwm = options.MultiHwm;
        long capacity = Math.Max(64L, Math.Max(clients, Math.Max(1, hwm)) * 2L);
        return capacity > int.MaxValue ? int.MaxValue : (int)capacity;
    }

    private static bool FlushPendingMessages(SocketBase server,
        Queue<PendingStreamMessage> pending, object pendingLock,
        ManualResetEventSlim pendingSignal, ref int rc)
    {
        while (true)
        {
            PendingStreamMessage? next = null;
            lock (pendingLock)
            {
                if (pending.Count > 0)
                    next = pending.Dequeue();
            }

            if (next == null)
                return true;

            SendStatus sendStatus = TrySendPendingMessage(server, next);
            if (sendStatus == SendStatus.Done)
            {
                next.Clear();
                continue;
            }

            if (sendStatus == SendStatus.Fatal)
            {
                next.Clear();
                rc = 1;
                return false;
            }

            lock (pendingLock)
            {
                pending.Enqueue(next);
            }
            pendingSignal.Set();
            return true;
        }
    }

    private static SendStatus TrySendPendingMessage(SocketBase server,
        PendingStreamMessage message)
    {
        while (message.Pending)
        {
            try
            {
                if (message.Payload == null)
                    return SendStatus.Fatal;

                if (((StreamSocket)server).Send(message.RoutingId,
                        message.Payload, SendFlags.DontWait))
                {
                    message.Clear();
                    return SendStatus.Done;
                }
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                            || IsInterrupted(ex.InternalErrno))
            {
                return SendStatus.Blocked;
            }
        }

        return SendStatus.Done;
    }

    private sealed class PendingStreamMessage
    {
        internal string RoutingId { get; private set; } = string.Empty;
        internal Message? Payload { get; private set; }
        internal bool Pending => Payload != null;

        internal void Assign(string routingId, Message payload)
        {
            RoutingId = routingId;
            Payload = payload;
        }

        internal void Clear()
        {
            Payload?.Dispose();
            Payload = null;
            RoutingId = string.Empty;
        }
    }

    private sealed class ControlState
    {
        internal int StopRequested;
    }
}
