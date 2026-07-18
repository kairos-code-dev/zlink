using System;
using System.Buffers.Binary;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using Systems.Zlink;

namespace SampleCommon;

public static class SampleSupport
{
    public static bool IsNativeAvailable()
    {
        try
        {
            _ = global::Systems.Zlink.Zlink.Version();
            return true;
        }
        catch (DllNotFoundException)
        {
            return false;
        }
        catch (EntryPointNotFoundException)
        {
            return false;
        }
        catch (TypeInitializationException ex) when (ex.InnerException
                is DllNotFoundException or EntryPointNotFoundException)
        {
            return false;
        }
    }

    public static string NewEndpoint(string transport, string prefix)
    {
        int port = ReservePort();
        return $"{transport}://127.0.0.1:{port}";
    }

    public static RoutingId RoutingIdUtf8(string value)
    {
        return RoutingId.From(Encoding.UTF8.GetBytes(value));
    }

    public static int ReservePort()
    {
        TcpListener listener = new(IPAddress.Loopback, 0);
        listener.Start();
        int port = ((IPEndPoint)listener.LocalEndpoint).Port;
        listener.Stop();
        return port;
    }

    public static void WaitConnected(params ISocketMonitor[] monitors)
    {
        foreach (ISocketMonitor monitor in monitors)
            WaitMonitorEvent(monitor, 5000, SocketEvent.ConnectionReady);
    }

    public static MonitorEvent WaitMonitorEvent(ISocketMonitor monitor,
        int timeoutMs, params SocketEvent[] expectedEvents)
    {
        if (expectedEvents == null || expectedEvents.Length == 0)
        {
            throw new ArgumentException("Expected monitor events are required.",
                nameof(expectedEvents));
        }

        _ = timeoutMs;
        MonitorEvent evt = monitor.Recv()
            ?? throw new TimeoutException("Timed out waiting for monitor event.");
        for (int i = 0; i < expectedEvents.Length; i++)
        {
            if ((SocketEvent)evt.Event == expectedEvents[i])
                return evt;
        }

        throw new InvalidOperationException(
            $"Unexpected monitor event {evt.Event}.");
    }

    public static void WaitOrThrow(Func<bool> predicate, int timeoutMs,
        string message)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            if (predicate())
                return;
            Thread.Sleep(10);
        }

        throw new TimeoutException(message);
    }

    /// <summary>
    ///     Configures a MeshNode with the identity, bind endpoint, and channel
    ///     Core requires before <c>Start</c> (a unique routing id, a bind, and at
    ///     least one channel), then starts it.
    /// </summary>
    public static void StartConfiguredNode(IMeshNode node, string routingId,
        string channelName = "app", string bind = "tcp://127.0.0.1:*")
    {
        node.SetRoutingId(RoutingId.From(routingId));
        node.SetBind(bind);
        node.AddChannel(channelName);
        node.Start();
    }

    /// <summary>Creates and starts a STREAM session service for a stream socket.</summary>
    public static IStreamSessionService StartSessionService(IMeshNode node,
        IStreamSocket stream)
    {
        IStreamSessionService service = node.CreateStreamSessionService(stream);
        service.Start();
        return service;
    }

    /// <summary>
    ///     Binds an actor to a STREAM session through the session service and
    ///     waits for the bind completion on the node's ready index.
    /// </summary>
    public static void BindSessionActor(IMeshNode node,
        IStreamSessionService service, RoutingId sessionRid, ActorRef actor,
        int timeoutMs = 5000)
    {
        SubmitResult rc = service.BindActor(sessionRid, actor,
            out MeshOperationId op);
        if (rc != SubmitResult.Ok)
            throw new InvalidOperationException(
                $"stream session bind submit failed: {rc}");
        WaitStreamOperation(node, op, "bind", timeoutMs);
    }

    /// <summary>Unbinds an actor from a STREAM session through the session service.</summary>
    public static void UnbindSessionActor(IMeshNode node,
        IStreamSessionService service, RoutingId sessionRid, ActorRef actor,
        int timeoutMs = 5000)
    {
        ulong generation = 0;
        foreach (StreamSessionBinding binding in service.Bindings(sessionRid))
        {
            if (string.Equals(binding.Actor.ActorId, actor.ActorId,
                    StringComparison.Ordinal))
                generation = binding.BindingGeneration;
        }

        SubmitResult rc = service.UnbindActor(sessionRid, actor, generation,
            out MeshOperationId op);
        if (rc != SubmitResult.Ok)
            throw new InvalidOperationException(
                $"stream session unbind submit failed: {rc}");
        WaitStreamOperation(node, op, "unbind", timeoutMs);
    }

    /// <summary>Relays a payload to a session-bound actor via the session service.</summary>
    public static void RelaySessionMessage(IStreamSessionService service,
        RoutingId sessionRid, ActorRef actor, string payload)
    {
        using Message message = Message.From(payload);
        SubmitResult rc = service.SendToActor(sessionRid, actor,
            new[] { message });
        if (rc != SubmitResult.Ok)
            throw new InvalidOperationException(
                $"stream session relay failed: {rc}");
    }

    private static void WaitStreamOperation(IMeshNode node, MeshOperationId opId,
        string name, int timeoutMs)
    {
        using var ready = new MeshReadyBatch();
        using var recv = new MeshReceiveBatch();
        bool completed = false;
        int terminal = 0;
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (!completed && DateTime.UtcNow < deadline)
        {
            PumpReady(node, ready, recv, (record, batch, index) =>
            {
                if (record.Kind == MeshRecordKind.Completion
                    && record.OperationId.Equals(opId))
                {
                    terminal = record.TerminalResult;
                    completed = true;
                }
            });
            if (completed)
                break;
            Thread.Sleep(10);
        }

        if (!completed)
            throw new TimeoutException(
                $"stream session {name} did not complete");
        if (terminal != 0)
            throw new InvalidOperationException(
                $"stream session {name} failed: {terminal}");
    }

    public static void WaitSpotPeerConnected(IMeshNode node, int timeoutMs = 5000)
    {
        WaitOrThrow(
            () => node.Status().AdmittedPeerCount > 0,
            timeoutMs,
            "mesh peer admission");
    }

    /// <summary>
    ///     Drains the node's ready index once and invokes <paramref name="handler" />
    ///     for every received record. The handler may take ownership of a record's
    ///     parts with <c>batch.RetainMessage(index)</c> and answer request or control
    ///     records with <c>record.Reply(...)</c>.
    /// </summary>
    public static void PumpReady(IMeshNode node, MeshReadyBatch ready,
        MeshReceiveBatch recv,
        Action<MeshReceiveRecord, MeshReceiveBatch, int> handler,
        MeshReadyDomains domains = MeshReadyDomains.All)
    {
        ready.Reset();
        node.DrainReady(domains, ready, RecvFlags.DontWait);
        int readyCount = ready.Count;
        for (int i = 0; i < readyCount; i++)
        {
            using MeshClaim claim = ready.TakeClaim(i);
            recv.Reset();
            while (claim.Receive(recv, RecvFlags.DontWait))
            {
                int count = recv.Count;
                for (int r = 0; r < count; r++)
                    handler(recv[r], recv, r);
                recv.Reset();
            }
        }
    }

    /// <summary>
    ///     Joins a local actor to a spot hosted on the same node. Submits the join,
    ///     admits the incoming host-side control record with <paramref name="admitPayload" />,
    ///     waits for the join completion, and returns the actor's membership epoch
    ///     (needed later to leave). Optionally verifies the join payload the host
    ///     observed.
    /// </summary>
    public static ulong JoinLocalSpot(IMeshNode node, ActorRef actor, ISpot spot,
        string joinPayload, string admitPayload = "accepted",
        Action<string>? onHostObservedJoin = null, int timeoutMs = 5000)
    {
        using var ready = new MeshReadyBatch();
        using var recv = new MeshReceiveBatch();

        MeshOperationId joinOp;
        using (Message join = Message.From(joinPayload))
        {
            joinOp = node.JoinSpot(actor, node.RoutingId, spot.RoutingId,
                spot.Status().LifecycleGeneration, new[] { join });
        }

        bool completed = false;
        int terminal = -1;
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (!completed && DateTime.UtcNow < deadline)
        {
            PumpReady(node, ready, recv, (record, batch, index) =>
            {
                if (record.Kind == MeshRecordKind.SpotControl
                    && record.OperationKind == MeshOperationKind.ActorJoin)
                {
                    // 호스트가 join 요청 레코드를 받아 admit(응답)한다. actor join은
                    // 전용 join-reply 경로(record.Accept)로만 승인할 수 있다.
                    Message[] request = batch.RetainMessage(index);
                    if (onHostObservedJoin != null && request.Length > 0)
                        onHostObservedJoin(request[0].GetString());
                    using (Message ok = Message.From(admitPayload))
                        record.Accept(new[] { ok });
                    MultipartCloseSafe(request);
                }
                else if (record.Kind == MeshRecordKind.Completion
                    && record.OperationKind == MeshOperationKind.ActorJoin
                    && record.OperationId.Equals(joinOp))
                {
                    terminal = record.TerminalResult;
                    completed = true;
                }
            });
            if (completed)
                break;
            Thread.Sleep(10);
        }

        if (!completed)
            throw new TimeoutException("actor join did not complete");
        if (terminal != 0)
            throw new InvalidOperationException($"actor join rejected: {terminal}");

        return node.ActorLookup(actor.ActorId, out ActorLocation location)
            ? location.MembershipEpoch
            : 0UL;
    }

    /// <summary>
    ///     Leaves a spot the actor previously joined and waits (best effort) for the
    ///     leave completion.
    /// </summary>
    public static void LeaveLocalSpot(IMeshNode node, ActorRef actor,
        ulong membershipEpoch, int timeoutMs = 5000)
    {
        using var ready = new MeshReadyBatch();
        using var recv = new MeshReceiveBatch();
        MeshOperationId leaveOp = node.LeaveSpot(actor, membershipEpoch);

        bool completed = false;
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (!completed && DateTime.UtcNow < deadline)
        {
            PumpReady(node, ready, recv, (record, batch, index) =>
            {
                if (record.Kind == MeshRecordKind.Completion
                    && record.OperationKind == MeshOperationKind.ActorLeave
                    && record.OperationId.Equals(leaveOp))
                {
                    completed = true;
                }
            });
            if (completed)
                break;
            Thread.Sleep(10);
        }
    }

    /// <summary>
    ///     Drains the node once and appends every actor-addressed message payload
    ///     into <paramref name="sink" /> in arrival order.
    /// </summary>
    public static void CollectActorMessages(IMeshNode node, MeshReadyBatch ready,
        MeshReceiveBatch recv, List<string> sink)
    {
        PumpReady(node, ready, recv, (record, batch, index) =>
        {
            if (record.Kind != MeshRecordKind.ActorSend || record.PartCount == 0)
                return;
            Message[] parts = batch.RetainMessage(index);
            sink.Add(parts[0].GetString());
            MultipartCloseSafe(parts);
        });
    }

    private static void MultipartCloseSafe(Message[] parts)
    {
        if (parts.Length > 0)
            Zlink.MultipartClose(parts);
    }

    public static string ReceiveUtf8(IMessageSocket socket, int timeoutMs)
    {
        _ = timeoutMs;
        using var received = Received.Create();
        if (!socket.Recv(received))
            throw new InvalidOperationException("recv failed");
        if (received.Parts.Count == 0)
            throw new InvalidOperationException(
                "Expected at least one message part.");
        return Encoding.UTF8.GetString(received.Parts[0].AsReadOnlySpan());
    }

    public static string SubscribeUtf8(ISubscriberSocket socket, out string topic,
        int timeoutMs)
    {
        _ = timeoutMs;
        using var subscribed = new TopicMessage();
        socket.Subscribe(subscribed);
        topic = subscribed.Topic;
        if (subscribed.Parts.Count == 0)
            throw new InvalidOperationException(
                "Expected at least one subscribed message part.");
        return Encoding.UTF8.GetString(subscribed.Parts[0].AsReadOnlySpan());
    }

    public static TcpClient ConnectRawClient(int port)
    {
        var client = new TcpClient();
        client.NoDelay = true;
        client.Connect(IPAddress.Loopback, port);
        return client;
    }

    public static void SendAll(NetworkStream stream, ReadOnlySpan<byte> payload)
    {
        stream.Write(payload);
        stream.Flush();
    }

    public static void SendStreamPacket(NetworkStream stream,
        ReadOnlySpan<byte> body, ReadOnlySpan<byte> header = default)
    {
        if (header.Length > ushort.MaxValue)
        {
            throw new ArgumentOutOfRangeException(nameof(header),
                "Stream packet header is too large.");
        }

        byte[] frame = new byte[6 + header.Length + body.Length];
        BinaryPrimitives.WriteUInt16BigEndian(frame.AsSpan(0, 2),
            (ushort)header.Length);
        BinaryPrimitives.WriteUInt32BigEndian(frame.AsSpan(2, 4),
            (uint)body.Length);
        header.CopyTo(frame.AsSpan(6, header.Length));
        body.CopyTo(frame.AsSpan(6 + header.Length, body.Length));
        SendAll(stream, frame);
    }

    public static byte[] ReceiveExact(NetworkStream stream, int size)
    {
        byte[] buffer = new byte[size];
        int read = 0;
        while (read < size)
        {
            int n = stream.Read(buffer, read, size - read);
            if (n <= 0)
                throw new IOException("stream receive timeout");
            read += n;
        }

        return buffer;
    }

    public static int ExtractPort(string endpoint)
    {
        int idx = endpoint.LastIndexOf(':');
        return int.Parse(endpoint.AsSpan(idx + 1));
    }

    public static void EnsureEqual(string expected, string actual, string name)
    {
        if (!string.Equals(expected, actual, StringComparison.Ordinal))
        {
            throw new InvalidOperationException(
                $"Expected {name} \"{expected}\" but received \"{actual}\".");
        }
    }

}
