using System.Text;
using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Channels;
using Zlink.Framework.Runtime.Codecs;

namespace Zlink.Framework.UnitTests.Runtime;

public sealed class RouteCodecTests
{
    [Fact]
    public async Task RouteHandlerInvoker_Uses_Configured_Codec()
    {
        var codecs = new ZLinkCodecRegistryBuilder();
        codecs.AddSerializer("application/route-test", new RouteProbeSerializer());
        var services = new ServiceCollection()
            .AddSingleton<RouteProbeHandler>()
            .BuildServiceProvider();
        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Request,
            "play",
            "Probe",
            ZLinkEnvelopeCodec.DefaultContentType,
            null,
            null,
            null,
            null,
            null);
        var parts = ZLinkEnvelopeCodec.EncodeParts(header, new RouteProbe("hello"), typeof(RouteProbe), codecs);
        var descriptor = new ZLinkRouteHandlerDescriptor(
            ZLinkMessageKind.Request,
            "play",
            "Probe",
            typeof(RouteProbeHandler),
            typeof(RouteProbe),
            typeof(RouteProbeReply),
            ZLinkHandlerMethodInvokerFactory.Create(
                typeof(RouteProbeHandler).GetMethod(nameof(RouteProbeHandler.HandleAsync))!));

        var invoker = new ZLinkRouteHandlerInvoker(services, codecs);

        var reply = await invoker.InvokeRequestAsync(
            descriptor,
            "play",
            RoutingId.From("source-node"),
            ZLinkEnvelopeCodec.DecodeHeader(parts),
            parts,
            CancellationToken.None);

        Assert.Equal("hello", RouteProbeHandler.LastRequest?.Text);
        Assert.Equal("application/route-test", ZLinkEnvelopeCodec.DecodeHeader(parts).ContentType);
        Assert.Equal(new RouteProbeReply("HELLO"), reply.Message);
    }

    [Fact]
    public void RouteReplyWriter_Uses_Configured_Codec()
    {
        var codecs = new ZLinkCodecRegistryBuilder();
        codecs.AddSerializer("application/route-test", new RouteProbeSerializer());
        var router = new RecordingRouter();
        var requestHeader = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Request,
            "play",
            "Probe",
            ZLinkEnvelopeCodec.DefaultContentType,
            "corr-1",
            null,
            null,
            null,
            null);

        ZLinkChannelReplyWriter.ReplyEnvelope(
            router,
            RoutingId.From("source-node"),
            7,
            ZLinkChannelReplyWriter.CreateReplyHeader(
                ZLinkMessageKind.Response,
                "play",
                requestHeader),
            new RouteProbe("reply"),
            typeof(RouteProbe),
            codecs);

        Assert.Equal("application/route-test", router.ReplyContentType);
        Assert.Equal("ROUTE:reply", router.ReplyBody);
    }

    [Fact]
    public void RouteChannelRuntime_DrainsSpotRouteBridge_AfterAcceptedSend()
    {
        var runtime = CreateRouteChannelRuntime();
        var bridge = new RecordingSpotRouteBridge(sendAccepted: true, requestAccepted: true);
        runtime.AttachSpotRouteBridge(bridge, null!);
        var parts = new[] { Message.From("payload") };

        try
        {
            Assert.True(runtime.TrySendViaSpotRouteBridge(
                RoutingId.From("target-node"),
                RoutingId.From("target-spot"),
                parts));
            Assert.Equal(1, bridge.SendCount);
            Assert.Equal(SendFlags.DontWait, bridge.LastSendFlags);
            Assert.Equal(1, bridge.DrainCount);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(parts);
        }
    }

    [Fact]
    public void RouteChannelRuntime_DrainsSpotRouteBridge_AfterAcceptedRequest()
    {
        var runtime = CreateRouteChannelRuntime();
        var bridge = new RecordingSpotRouteBridge(sendAccepted: true, requestAccepted: true);
        runtime.AttachSpotRouteBridge(bridge, null!);
        var parts = new[] { Message.From("payload") };

        try
        {
            Assert.True(runtime.TryRequestViaSpotRouteBridge(
                RoutingId.From("target-node"),
                RoutingId.From("target-spot"),
                parts,
                static (_, _) => { },
                TimeSpan.FromSeconds(1)));
            Assert.Equal(1, bridge.RequestCount);
            Assert.Equal(SendFlags.DontWait, bridge.LastRequestFlags);
            Assert.Equal(1, bridge.DrainCount);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(parts);
        }
    }

    [Fact]
    public void RouteConnectionSet_RidAwareConnect_SetsProbeBeforeConnect()
    {
        var router = new RecordingConnectRouter();
        var connections = new ZLinkRouteConnectionSet(router);
        var peerRid = RoutingId.From("route-peer");

        connections.Connect(peerRid, "inproc://route-peer");

        Assert.Equal(peerRid, router.ConnectRoutingId);
        Assert.True(router.ProbeEnabled);
        Assert.Equal(["connect-rid", "probe", "connect"], router.Events);
        Assert.Equal("inproc://route-peer", Assert.Single(router.Connected));
    }

    [Fact]
    public async Task RouterProbe_AllowsNonInitiatorRidAddressedSendOverInboundIdentity()
    {
        await using var context = Systems.Zlink.Zlink.CreateContext();
        await using var initiator = context.CreateRouterSocket();
        await using var nonInitiator = context.CreateRouterSocket();
        var initiatorRid = RoutingId.From("route-a-initiator");
        var nonInitiatorRid = RoutingId.From("route-z-non-initiator");
        var endpoint = $"inproc://route-inbound-identity-{Guid.NewGuid():N}";

        initiator.SetRoutingId(initiatorRid);
        nonInitiator.SetRoutingId(nonInitiatorRid);
        initiator.Options.Linger = TimeSpan.Zero;
        nonInitiator.Options.Linger = TimeSpan.Zero;
        nonInitiator.Options.Mandatory = true;
        nonInitiator.Bind(endpoint);
        initiator.Options.SetConnectRoutingId(nonInitiatorRid);
        initiator.Options.Probe = true;
        initiator.Connect(endpoint);

        await SendUntilReceivedAsync(
            nonInitiator,
            initiator,
            initiatorRid,
            "reply-from-non-initiator",
            TimeSpan.FromSeconds(3));
    }

    private static ZLinkRouteChannelRuntime CreateRouteChannelRuntime()
    {
        var services = new ServiceCollection().BuildServiceProvider();
        return new ZLinkRouteChannelRuntime(
            services,
            new ZLinkFrameworkRegistration(),
            new ZLinkRouteChannelRegistration { RouterChannelId = "route-test" },
            new RecordingRouter(),
            new ZLinkRouteHandlerRegistry([]),
            null,
            CancellationToken.None);
    }

    private static async Task SendUntilReceivedAsync(
        IRouterSocket sender,
        IRouterSocket receiver,
        RoutingId targetRid,
        string payload,
        TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        using var received = Received.Create();
        while (DateTime.UtcNow < deadline)
        {
            using (var message = Message.From(payload))
            {
                try
                {
                    _ = sender.Send(targetRid).Message(message).Submit();
                }
                catch (ZlinkException)
                {
                }
            }

            if (receiver.Recv(received, RecvFlags.DontWait))
            {
                Assert.Equal(payload, received.SinglePartOrThrow().GetString());
                return;
            }

            await Task.Delay(10);
        }

        throw new TimeoutException("Timed out waiting for inbound identity routed send.");
    }

    private sealed record RouteProbe(string Text);

    private sealed record RouteProbeReply(string Text);

    private sealed class RouteProbeHandler : IZLinkRouteRequestHandler<RouteProbe, RouteProbeReply>
    {
        public static RouteProbe? LastRequest { get; private set; }

        public ValueTask<RouteProbeReply> HandleAsync(
            RouteProbe request,
            ZLinkRouteRequestContext context,
            CancellationToken cancellationToken)
        {
            _ = context;
            _ = cancellationToken;
            LastRequest = request;
            return ValueTask.FromResult(new RouteProbeReply(request.Text.ToUpperInvariant()));
        }
    }

    private sealed class RouteProbeSerializer : IZLinkMessageSerializer
    {
        public ZLinkEncodedPayload Serialize(object value, Type type)
        {
            var text = value switch
            {
                RouteProbe probe => probe.Text,
                RouteProbeReply reply => reply.Text,
                _ => throw new NotSupportedException(type.FullName)
            };
            return ZLinkEncodedPayload.From(Encoding.UTF8.GetBytes("ROUTE:" + text));
        }

        public object? Deserialize(ZLinkEncodedPayload payload, Type type)
        {
            var text = Encoding.UTF8.GetString(payload.Bytes.Span);
            var value = text.StartsWith("ROUTE:", StringComparison.Ordinal)
                ? text["ROUTE:".Length..]
                : text;
            if (type == typeof(RouteProbe)) return new RouteProbe(value);
            if (type == typeof(RouteProbeReply)) return new RouteProbeReply(value);
            throw new NotSupportedException(type.FullName);
        }
    }

    private sealed class RecordingRouter : IZLinkBackendRouterSocket
    {
        public string? ReplyContentType { get; private set; }

        public string? ReplyBody { get; private set; }

        public object NativeInstance => this;

        public ValueTask DisposeAsync()
        {
            return ValueTask.CompletedTask;
        }

        public void Bind(string endpoint)
        {
            throw new NotSupportedException();
        }

        public void SetChannelName(string channelName)
        {
            throw new NotSupportedException();
        }

        public void Connect(string endpoint)
        {
            throw new NotSupportedException();
        }

        public void Disconnect(string endpoint)
        {
            throw new NotSupportedException();
        }

        public void SetPeerWeight(int weight)
        {
            throw new NotSupportedException();
        }

        public int GetPeerWeight()
        {
            throw new NotSupportedException();
        }

        public void OnSendReady(Action handler)
        {
            _ = handler;
        }

        public void SetSendHighWaterMark(int value)
        {
            throw new NotSupportedException();
        }

        public void SetReceiveHighWaterMark(int value)
        {
            throw new NotSupportedException();
        }

        public void SetRoutingId(RoutingId routingId)
        {
            throw new NotSupportedException();
        }

        public void SetConnectRoutingId(RoutingId routingId)
        {
        }

        public void SetProbe(bool enabled)
        {
        }

        public void SetMandatory(bool mandatory)
        {
            throw new NotSupportedException();
        }

        public Received? Recv(RecvFlags flags = RecvFlags.None)
        {
            throw new NotSupportedException();
        }

        public bool Send(RoutingId routingId, Message message, SendFlags flags)
        {
            throw new NotSupportedException();
        }

        public bool Send(RoutingId routingId, IReadOnlyList<Message> parts, SendFlags flags)
        {
            throw new NotSupportedException();
        }

        public bool Request(
            RoutingId routingId,
            Message message,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout)
        {
            throw new NotSupportedException();
        }

        public bool Request(
            RoutingId routingId,
            IReadOnlyList<Message> parts,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout)
        {
            throw new NotSupportedException();
        }

        public bool SendToSpot(
            RoutingId targetNodeRid,
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            SendFlags flags)
        {
            throw new NotSupportedException();
        }

        public bool RequestToSpot(
            RoutingId targetNodeRid,
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout)
        {
            throw new NotSupportedException();
        }

        public void Reply(RoutingId routingId, ulong requestSeq, Message message)
        {
            throw new NotSupportedException();
        }

        public void Reply(RoutingId routingId, ulong requestSeq, IReadOnlyList<Message> parts)
        {
            _ = routingId;
            _ = requestSeq;
            ReplyContentType = ZLinkEnvelopeCodec.DecodeHeader(parts).ContentType;
            ReplyBody = parts[1].GetString();
        }
    }

    private sealed class RecordingConnectRouter : IZLinkBackendRouterSocket
    {
        public List<string> Events { get; } = [];

        public List<string> Connected { get; } = [];

        public RoutingId? ConnectRoutingId { get; private set; }

        public bool ProbeEnabled { get; private set; }

        public object NativeInstance => this;

        public ValueTask DisposeAsync()
        {
            return ValueTask.CompletedTask;
        }

        public void Bind(string endpoint)
        {
            throw new NotSupportedException();
        }

        public void SetChannelName(string channelName)
        {
            throw new NotSupportedException();
        }

        public void Connect(string endpoint)
        {
            Events.Add("connect");
            Connected.Add(endpoint);
        }

        public void Disconnect(string endpoint)
        {
            throw new NotSupportedException();
        }

        public void SetPeerWeight(int weight)
        {
            throw new NotSupportedException();
        }

        public int GetPeerWeight()
        {
            throw new NotSupportedException();
        }

        public void OnSendReady(Action handler)
        {
            _ = handler;
        }

        public void SetSendHighWaterMark(int value)
        {
            throw new NotSupportedException();
        }

        public void SetReceiveHighWaterMark(int value)
        {
            throw new NotSupportedException();
        }

        public void SetRoutingId(RoutingId routingId)
        {
            throw new NotSupportedException();
        }

        public void SetConnectRoutingId(RoutingId routingId)
        {
            Events.Add("connect-rid");
            ConnectRoutingId = routingId;
        }

        public void SetProbe(bool enabled)
        {
            Events.Add("probe");
            ProbeEnabled = enabled;
        }

        public void SetMandatory(bool mandatory)
        {
            throw new NotSupportedException();
        }

        public Received? Recv(RecvFlags flags = RecvFlags.None)
        {
            throw new NotSupportedException();
        }

        public bool Send(RoutingId routingId, Message message, SendFlags flags)
        {
            throw new NotSupportedException();
        }

        public bool Send(RoutingId routingId, IReadOnlyList<Message> parts, SendFlags flags)
        {
            throw new NotSupportedException();
        }

        public bool Request(
            RoutingId routingId,
            Message message,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout)
        {
            throw new NotSupportedException();
        }

        public bool Request(
            RoutingId routingId,
            IReadOnlyList<Message> parts,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout)
        {
            throw new NotSupportedException();
        }

        public bool SendToSpot(
            RoutingId targetNodeRid,
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            SendFlags flags)
        {
            throw new NotSupportedException();
        }

        public bool RequestToSpot(
            RoutingId targetNodeRid,
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout)
        {
            throw new NotSupportedException();
        }

        public void Reply(RoutingId routingId, ulong requestSeq, Message message)
        {
            throw new NotSupportedException();
        }

        public void Reply(RoutingId routingId, ulong requestSeq, IReadOnlyList<Message> parts)
        {
            throw new NotSupportedException();
        }
    }

    private sealed class RecordingSpotRouteBridge(
        bool sendAccepted,
        bool requestAccepted) : IZLinkBackendSpotRouteBridge
    {
        public int SendCount { get; private set; }

        public int RequestCount { get; private set; }

        public int DrainCount { get; private set; }

        public SendFlags LastSendFlags { get; private set; }

        public SendFlags LastRequestFlags { get; private set; }

        public object NativeInstance => this;

        public ValueTask DisposeAsync()
        {
            return ValueTask.CompletedTask;
        }

        public void AttachRouterChannel(
            string channelName,
            IZLinkBackendRouterSocket router,
            SpotRouteBridgeEndpointOptions? options = null)
        {
            _ = channelName;
            _ = router;
            _ = options;
        }

        public bool Send(
            string channelName,
            RoutingId targetNodeRid,
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            SendFlags flags)
        {
            _ = channelName;
            _ = targetNodeRid;
            _ = targetSpotRid;
            _ = parts;
            LastSendFlags = flags;
            SendCount++;
            return sendAccepted;
        }

        public bool Request(
            string channelName,
            RoutingId targetNodeRid,
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout)
        {
            _ = channelName;
            _ = targetNodeRid;
            _ = targetSpotRid;
            _ = parts;
            _ = callback;
            LastRequestFlags = flags;
            _ = timeout;
            RequestCount++;
            return requestAccepted;
        }

        public bool HandleRouterReceived(
            string channelName,
            RoutingId sourceNodeRid,
            ulong requestSeq,
            IReadOnlyList<Message> parts)
        {
            _ = channelName;
            _ = sourceNodeRid;
            _ = requestSeq;
            _ = parts;
            return false;
        }

        public void Drain()
        {
            DrainCount++;
        }
    }
}
