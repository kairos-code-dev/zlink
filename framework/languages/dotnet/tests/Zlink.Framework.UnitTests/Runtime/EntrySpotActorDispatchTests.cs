using System.Buffers.Binary;
using System.Collections.Concurrent;
using System.Text;
using System.Text.Json;
using Microsoft.Extensions.DependencyInjection;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.UnitTests.Runtime;

public sealed class EntrySpotActorDispatchTests
{
    [Fact]
    public async Task EntrySpotActorDispatch_ConcurrentActors_StartsOutsideEntrySpotSerialLine_AndKeepsSameActorOrdering()
    {
        var probe = new DispatchProbe();
        var services = new ServiceCollection()
            .AddSingleton(probe)
            .AddTransient<ProbeActorSendHandler>()
            .BuildServiceProvider();
        var activation = CreateActivation(services);
        await using var _ = activation.ConfigureAwait(false);
        activation.Configure();

        Assert.True(activation.TryResolveActorPacket(
            typeof(ProbeActor),
            CreateHeader("first"),
            out var descriptor));
        Assert.NotNull(descriptor);

        var actorA = new ProbeActor("actor-a");
        var actorB = new ProbeActor("actor-b");
        var stateA = new ZLinkActorRuntimeState(actorA.ActorId);
        var stateB = new ZLinkActorRuntimeState(actorB.ActorId);

        var firstA = DispatchAsync(activation, descriptor, stateA, actorA, "first");
        await probe.ActorAFirstStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));

        var firstB = DispatchAsync(activation, descriptor, stateB, actorB, "first");
        await probe.ActorBStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));

        var secondA = DispatchAsync(activation, descriptor, stateA, actorA, "second");
        await Task.Delay(100);
        Assert.False(probe.ActorASecondStarted.Task.IsCompleted);

        probe.ReleaseActorAFirst.SetResult();
        await probe.ActorASecondStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await Task.WhenAll(firstA, firstB, secondA).WaitAsync(TimeSpan.FromSeconds(5));

        Assert.Equal(
            new[]
            {
                "actor-a:first:start",
                "actor-b:first:start",
                "actor-a:first:end",
                "actor-a:second:start"
            },
            probe.Events.ToArray());
    }

    [Fact]
    public async Task EntrySpotActorDispatch_YieldCall_Throws_WhenCreatedInsideActorHandler()
    {
        var services = new ServiceCollection()
            .AddTransient<ProbeActorYieldHandler>()
            .BuildServiceProvider();
        var activation = CreateActivation(services);
        await using var _ = activation.ConfigureAwait(false);
        activation.Configure();

        Assert.True(activation.TryResolveActorPacket(
            typeof(ProbeActor),
            CreateHeader("yield"),
            out var descriptor));
        Assert.NotNull(descriptor);

        var actor = new ProbeActor("actor-yield");
        var state = new ZLinkActorRuntimeState(actor.ActorId);
        var error = await Assert.ThrowsAsync<InvalidOperationException>(() =>
            DispatchAsync(activation, descriptor, state, actor, "yield"));

        Assert.Contains("Yield requires a framework Spot handler turn", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public async Task EntrySpotRouteDispatch_UsesRoutedMessagesAlreadyDrainedByBackendCallback()
    {
        var probe = new RouteDispatchProbe();
        var services = new ServiceCollection()
            .AddSingleton(probe)
            .AddTransient<ProbeRouteHandler>()
            .BuildServiceProvider();
        var spot = new CapturingSpot();
        var (activation, runtime) = CreateActivationWithRuntime(services, spot);
        await using var _ = activation.ConfigureAwait(false);
        activation.Configure();

        var pump = new ZLinkEntrySpotDispatchPump(
            runtime,
            activation,
            new ZLinkRuntimeTaskRunner(new ThrowingRuntimeErrorSink(), CancellationToken.None));
        pump.Attach(spot);

        var received = CreateRoutedReceived("routed-ok");
        spot.RaiseDispatch(new ZLinkBackendSpotDispatchInfo(
            ZLinkBackendSpotDispatchEvent.RouteReadable,
            RoutedMessages: [received]));

        Assert.Equal(
            "routed-ok",
            await probe.Message.Task.WaitAsync(TimeSpan.FromSeconds(5)));
    }

    [Fact]
    public async Task EntrySpotActorDispatch_NoBindRequest_RepliesViaNoBind_AndDoesNotBindSession()
    {
        var node = new CapturingSpotNode();
        var (runtime, actorRef) = await CreateStartedRuntimeAsync(node);
        try
        {
            var actor = RegisterProbeActor(runtime, actorRef);

            await ZLinkEntrySpotActorDispatcher.DispatchAsync(
                runtime,
                null,
                CreateActorRequestParts(actorRef, "request", "ok", requestId: 42, flags: 1),
                CancellationToken.None);

            var reply = Assert.Single(node.NoBindReplies);
            Assert.Equal(actorRef, reply.Actor);
            Assert.Equal(RoutingId.From("source-node"), reply.SourceNodeRid);
            Assert.Equal(RoutingId.From("source-session"), reply.SourceSessionRid);
            Assert.Equal<ulong>(42, reply.RequestId);
            Assert.Equal<uint>(1, reply.Flags);
            Assert.Empty(node.BoundSessionReplies);
            Assert.False(runtime.TryGetActorBoundSession(actor.ActorId, out _));

            var decoded = DecodeReplyFrame<ProbeReply>(Assert.Single(reply.Parts));
            Assert.Equal(ZlinkStreamMessageKind.Response, decoded.Header.Kind);
            Assert.Equal("ok:actor-a", decoded.Payload.Value);
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task EntrySpotActorDispatch_BoundRequest_UsesBoundSession_AndBindsSession()
    {
        var node = new CapturingSpotNode();
        var (runtime, actorRef) = await CreateStartedRuntimeAsync(node);
        try
        {
            var actor = RegisterProbeActor(runtime, actorRef);

            await ZLinkEntrySpotActorDispatcher.DispatchAsync(
                runtime,
                null,
                CreateActorRequestParts(actorRef, "request", "bound", requestId: 0, flags: 0),
                CancellationToken.None);

            Assert.Empty(node.NoBindReplies);
            var boundReply = Assert.Single(node.BoundSessionReplies);
            Assert.Equal(actorRef, boundReply.Actor);
            Assert.True(runtime.TryGetActorBoundSession(actor.ActorId, out var boundSession));
            Assert.Equal(RoutingId.From("source-session"), boundSession.SessionRid);

            var decoded = DecodeReplyFrame<ProbeReply>(Assert.Single(boundReply.Parts));
            Assert.Equal(ZlinkStreamMessageKind.Response, decoded.Header.Kind);
            Assert.Equal("bound:actor-a", decoded.Payload.Value);
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task EntrySpotActorDispatch_NoBindHandlerException_RepliesNoBindError()
    {
        var node = new CapturingSpotNode();
        var observer = new CapturingMessageFlowObserver();
        var (runtime, actorRef) = await CreateStartedRuntimeAsync(node, observer);
        try
        {
            RegisterProbeActor(runtime, actorRef);

            await ZLinkEntrySpotActorDispatcher.DispatchAsync(
                runtime,
                null,
                CreateActorRequestParts(actorRef, "throw", "boom", requestId: 43, flags: 1),
                CancellationToken.None);

            var reply = Assert.Single(node.NoBindReplies);
            var decoded = DecodeReplyFrame<ZLinkStreamWireError>(Assert.Single(reply.Parts));
            Assert.Equal(ZlinkStreamMessageKind.Error, decoded.Header.Kind);
            Assert.Equal(nameof(InvalidOperationException), decoded.Payload.Code);
            Assert.Contains("boom", decoded.Payload.Message, StringComparison.Ordinal);
            Assert.Empty(node.BoundSessionReplies);
            Assert.False(runtime.TryGetActorBoundSession(actorRef.ActorId, out _));

            var observed = await observer.WaitAsync(TimeSpan.FromSeconds(2));
            Assert.Equal(ZLinkMessageFlowOutcome.Error, observed.Outcome);
            Assert.Equal(ZLinkDispatchErrorSurface.SpotActor, observed.Surface);
            Assert.Equal(ZLinkDispatchMessageKind.ActorRequest, observed.MessageKind);
            Assert.Equal(ZLinkDispatchErrorReason.HandlerException, observed.ErrorReason);
            Assert.Equal(ZLinkDispatchErrorAction.ReplyError, observed.ErrorAction);
            Assert.Equal("throw", observed.PacketName);
            Assert.Equal("actor-a", observed.ActorId);
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task EntrySpotActorDispatch_NoBindMissingActor_RepliesNoBindError()
    {
        var node = new CapturingSpotNode();
        var (runtime, actorRef) = await CreateStartedRuntimeAsync(node);
        try
        {
            await ZLinkEntrySpotActorDispatcher.DispatchAsync(
                runtime,
                null,
                CreateActorRequestParts(actorRef, "request", "missing", requestId: 44, flags: 1),
                CancellationToken.None);

            var reply = Assert.Single(node.NoBindReplies);
            var decoded = DecodeReplyFrame<ZLinkStreamWireError>(Assert.Single(reply.Parts));
            Assert.Equal(ZlinkStreamMessageKind.Error, decoded.Header.Kind);
            Assert.Contains("not available", decoded.Payload.Message, StringComparison.Ordinal);
            Assert.Empty(node.BoundSessionReplies);
            Assert.False(runtime.TryGetActorBoundSession(actorRef.ActorId, out _));
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    private static async Task DispatchAsync(
        ZLinkEntrySpotActivation activation,
        ZLinkSpotActorPacketDescriptor descriptor,
        ZLinkActorRuntimeState state,
        ProbeActor actor,
        string name)
    {
        var header = CreateHeader(name);
        using var body = Message.From(Encoding.UTF8.GetBytes(name));
        await state.ExecuteDispatchAsync(
                header,
                ct => activation.InvokeActorPacketAsync(
                    descriptor,
                    actor,
                    header,
                    body,
                    ct),
                CancellationToken.None)
            .ConfigureAwait(false);
    }

    private static ZlinkStreamHeader CreateHeader(string name)
    {
        return new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Send,
            ZlinkStreamCodec.Raw,
            ZlinkStreamHeaderFlags.None,
            null,
            name,
            ZlinkStreamMetadata.Empty);
    }

    private static ZLinkEntrySpotActivation CreateActivation(IServiceProvider services)
    {
        return CreateActivationWithRuntime(services, new CapturingSpot()).Activation;
    }

    private static async Task<(ZLinkFrameworkRuntime Runtime, ZLinkBackendActorRef ActorRef)> CreateStartedRuntimeAsync(
        CapturingSpotNode node,
        IZLinkMessageFlowObserver? messageFlowObserver = null)
    {
        var services = new ServiceCollection()
            .AddTransient<ProbeActorRequestHandler>()
            .AddTransient<ProbeActorThrowingRequestHandler>()
            .BuildServiceProvider();
        var registration = new ZLinkFrameworkRegistration
        {
            DefaultRequestTimeout = TimeSpan.FromSeconds(1)
        };
        if (messageFlowObserver is not null)
            registration.DispatchOptions.SetMessageFlowObserver(messageFlowObserver);
        registration.SpotNodes["entry"] = new ZLinkSpotNodeRegistration
        {
            SpotNodeName = "entry",
            RoutingId = RoutingId.From("entry-node"),
            Router = new ZLinkSpotRouterCapabilityRegistration { BindEndpoint = "inproc://entry" },
            EntrySpotType = typeof(ProbeEntrySpot),
            ActorFactories =
            {
                ["probe"] = typeof(ProbeActorFactory)
            }
        };
        var runtime = new ZLinkFrameworkRuntime(
            services,
            new CapturingBackendAdapterFactory(node),
            registration,
            new ZLinkHandlerRegistry([]),
            new ZLinkHandlerDispatcher(services.GetRequiredService<IServiceScopeFactory>(), registration));
        await runtime.StartAsync(CancellationToken.None);
        return (runtime, new ZLinkBackendActorRef(RoutingId.From("actor-node"), "actor-a", 1));
    }

    private static (ZLinkEntrySpotActivation Activation, ZLinkFrameworkRuntime Runtime) CreateActivationWithRuntime(
        IServiceProvider services,
        IZLinkBackendSpot spot)
    {
        var registration = new ZLinkFrameworkRegistration();
        var runtime = new ZLinkFrameworkRuntime(
            services,
            new ThrowingBackendAdapterFactory(),
            registration,
            new ZLinkHandlerRegistry([]),
            new ZLinkHandlerDispatcher(services.GetRequiredService<IServiceScopeFactory>(), registration));

        return (new ZLinkEntrySpotActivation(
            runtime,
            services,
            spot,
            typeof(ProbeEntrySpot),
            RoutingId.From("entry-node"),
            "entry",
            "entry-channel",
            TimeSpan.FromSeconds(5),
            TimeSpan.FromSeconds(1)), runtime);
    }

    private static ProbeActor RegisterProbeActor(
        ZLinkFrameworkRuntime runtime,
        ZLinkBackendActorRef actorRef)
    {
        var actor = new ProbeActor(actorRef.ActorId);
        var state = runtime.GetOrCreateActorState(actor.ActorId);
        state.Actor = actor;
        state.NativeActorRef = actorRef;
        return actor;
    }

    private static IReadOnlyList<ZLinkBackendActorPart> CreateActorRequestParts(
        ZLinkBackendActorRef actorRef,
        string packetName,
        string value,
        ulong requestId,
        uint flags)
    {
        var header = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Request,
            ZlinkStreamCodec.Json,
            ZlinkStreamHeaderFlags.HasRequestSeq,
            new ZlinkStreamRequestSeq(7),
            packetName,
            ZlinkStreamMetadata.Empty,
            "corr-1");
        return
        [
            new ZLinkBackendActorPart(
                actorRef,
                RoutingId.From("source-node"),
                RoutingId.From("source-session"),
                requestId,
                flags,
                Message.From(ZLinkStreamProtocolDefaults.EncodeHeader(header).Span),
                true),
            new ZLinkBackendActorPart(
                actorRef,
                RoutingId.From("source-node"),
                RoutingId.From("source-session"),
                requestId,
                flags,
                Message.From(ZLinkEnvelopeCodec.EncodeJsonBytes(value, typeof(string))),
                false)
        ];
    }

    private static (ZlinkStreamHeader Header, T Payload) DecodeReplyFrame<T>(byte[] frame)
    {
        var headerSize = BinaryPrimitives.ReadUInt16BigEndian(frame.AsSpan(0, 2));
        var payloadSize = BinaryPrimitives.ReadUInt32BigEndian(frame.AsSpan(2, 4));
        var header = ZLinkStreamProtocolDefaults.DecodeHeader(frame.AsMemory(6, headerSize));
        var payload = JsonSerializer.Deserialize<T>(
                          frame.AsSpan(6 + headerSize, checked((int)payloadSize)),
                          ZLinkJsonSerializerOptions.Default)
                      ?? throw new InvalidOperationException("Reply payload was null.");
        return (header, payload);
    }

    private static Received CreateRoutedReceived(string value)
    {
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Command,
            "entry-channel",
            nameof(ProbeRouteMessage));
        var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(
            header,
            new ProbeRouteMessage(value));
        using var context = global::Systems.Zlink.Zlink.CreateContext();
        using var node = context.CreateSpotNode();
        using var sender = node.CreateSpot();
        using var receiver = node.CreateSpot();
        var nodeRid = RoutingId.From("route-source-node");
        var senderRid = RoutingId.From("route-source-spot");
        var receiverRid = RoutingId.From("route-receiver-spot");

        node.SetRoutingId(nodeRid);
        sender.SetRoutingId(senderRid);
        receiver.SetRoutingId(receiverRid);
        sender.SendToSpot(nodeRid, receiverRid)
            .Messages(parts)
            .Submit();

        var received = Received.Create();
        if (!WaitUntil(() => receiver.RecvRouted(received, RecvFlags.DontWait), TimeSpan.FromSeconds(5)))
            throw new TimeoutException("Timed out creating a routed Received message for the entry spot dispatch test.");
        return received;
    }

    private static bool WaitUntil(Func<bool> predicate, TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow.Add(timeout);
        while (DateTime.UtcNow < deadline)
        {
            if (predicate()) return true;
            Thread.Sleep(10);
        }

        return false;
    }

    private sealed class DispatchProbe
    {
        public ConcurrentQueue<string> Events { get; } = new();

        public TaskCompletionSource ActorAFirstStarted { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource ActorASecondStarted { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource ActorBStarted { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource ReleaseActorAFirst { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
    }

    private sealed class ProbeActor(string actorId) : IZLinkActor
    {
        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context => throw new NotSupportedException();
    }

    private sealed class ProbeActorFactory : IZLinkActorFactory
    {
        public ValueTask<IZLinkActor> CreateAsync(
            string actorId,
            IZLinkActorContext context,
            CancellationToken cancellationToken = default)
        {
            _ = context;
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult<IZLinkActor>(new ProbeActor(actorId));
        }
    }

    private sealed class ProbeEntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot
    {
        public IZLinkEntrySpotContext Context { get; } = context;

        public void Configure()
        {
            Context.Handlers.AddPacket<ProbeRouteHandler>();
            Context.Handlers.AddActorSend<ProbeActorSendHandler, ProbeActor>("first");
            Context.Handlers.AddActorSend<ProbeActorSendHandler, ProbeActor>("second");
            Context.Handlers.AddActorSend<ProbeActorYieldHandler, ProbeActor>("yield");
            Context.Handlers.AddActorRequest<ProbeActorRequestHandler, ProbeActor>("request");
            Context.Handlers.AddActorRequest<ProbeActorThrowingRequestHandler, ProbeActor>("throw");
        }
    }

    private sealed record ProbeRouteMessage(string Value);

    private sealed record ProbeReply(string Value);

    private sealed class RouteDispatchProbe
    {
        public TaskCompletionSource<string> Message { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
    }

    private sealed class ProbeRouteHandler(RouteDispatchProbe probe)
        : IZLinkSpotPacketHandler<ProbeEntrySpot, ProbeRouteMessage>
    {
        public ValueTask HandleAsync(
            ProbeEntrySpot spot,
            ProbeRouteMessage message,
            CancellationToken cancellationToken)
        {
            _ = spot;
            _ = cancellationToken;
            probe.Message.SetResult(message.Value);
            return ValueTask.CompletedTask;
        }
    }

    private sealed class ProbeActorSendHandler(DispatchProbe probe)
        : IZLinkEntrySpotActorSendHandler<ProbeEntrySpot, ProbeActor, string>
    {
        public async ValueTask HandleAsync(
            ProbeEntrySpot entrySpot,
            ProbeActor actor,
            ZLinkSpotActorSendContext context,
            string message,
            CancellationToken cancellationToken)
        {
            _ = entrySpot;
            _ = context;
            probe.Events.Enqueue($"{actor.ActorId}:{message}:start");

            if (actor.ActorId == "actor-a" && message == "first")
            {
                probe.ActorAFirstStarted.SetResult();
                await probe.ReleaseActorAFirst.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
                probe.Events.Enqueue($"{actor.ActorId}:{message}:end");
                return;
            }

            if (actor.ActorId == "actor-a" && message == "second")
                probe.ActorASecondStarted.SetResult();

            if (actor.ActorId == "actor-b")
                probe.ActorBStarted.SetResult();
        }
    }

    private sealed class ProbeActorYieldHandler
        : IZLinkEntrySpotActorSendHandler<ProbeEntrySpot, ProbeActor, string>
    {
        public async ValueTask HandleAsync(
            ProbeEntrySpot entrySpot,
            ProbeActor actor,
            ZLinkSpotActorSendContext context,
            string request,
            CancellationToken cancellationToken)
        {
            _ = actor;
            _ = context;
            await entrySpot.Context.Outbound.RequestToChannel(
                    "unused",
                    request)
                .PacketName("unused")
                .Yield<string>(cancellationToken);
        }
    }

    private sealed class ProbeActorRequestHandler
        : IZLinkEntrySpotActorRequestHandler<ProbeEntrySpot, ProbeActor, string, ProbeReply>
    {
        public ValueTask<ProbeReply> HandleAsync(
            ProbeEntrySpot entrySpot,
            ProbeActor actor,
            ZLinkSpotActorRequestContext context,
            string request,
            CancellationToken cancellationToken)
        {
            _ = entrySpot;
            _ = context;
            _ = cancellationToken;
            return ValueTask.FromResult(new ProbeReply($"{request}:{actor.ActorId}"));
        }
    }

    private sealed class ProbeActorThrowingRequestHandler
        : IZLinkEntrySpotActorRequestHandler<ProbeEntrySpot, ProbeActor, string, ProbeReply>
    {
        public ValueTask<ProbeReply> HandleAsync(
            ProbeEntrySpot entrySpot,
            ProbeActor actor,
            ZLinkSpotActorRequestContext context,
            string request,
            CancellationToken cancellationToken)
        {
            _ = entrySpot;
            _ = actor;
            _ = context;
            _ = cancellationToken;
            throw new InvalidOperationException($"boom:{request}");
        }
    }

    private sealed record CapturedActorReply(
        ZLinkBackendActorRef Actor,
        RoutingId SourceNodeRid,
        RoutingId SourceSessionRid,
        ulong RequestId,
        uint Flags,
        IReadOnlyList<byte[]> Parts);

    private sealed class CapturingSpot : IZLinkBackendSpot
    {
        private Action<ZLinkBackendSpotDispatchInfo>? _dispatchHandler;

        public object NativeInstance => this;

        public RoutingId RoutingId => RoutingId.From("entry-spot");

        public ValueTask DisposeAsync() => ValueTask.CompletedTask;

        public void SetRoutingId(RoutingId routingId) { }

        public void SetSubscription(string topic) { }

        public bool Subscribe(TopicMessage result, RecvFlags flags) => false;

        public bool RecvRoute(Received result, RecvFlags flags) => false;

        public void OnDispatchEvent(Action<ZLinkBackendSpotDispatchInfo> handler)
        {
            _dispatchHandler = handler;
        }

        public void RaiseDispatch(ZLinkBackendSpotDispatchInfo info)
        {
            (_dispatchHandler ?? throw new InvalidOperationException("Dispatch handler was not attached.")).Invoke(info);
        }

        public void OnSendReady(Action handler) { }

        public bool RequestToChannel(
            string channelName,
            Message message,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout) => false;

        public bool RequestToChannel(
            string channelName,
            IReadOnlyList<Message> parts,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout) => false;

        public bool SendToChannel(string channelName, Message message, SendFlags flags) => false;

        public bool SendToChannel(string channelName, IReadOnlyList<Message> parts, SendFlags flags) => false;

        public bool Publish(string topic, Message message, SendFlags flags) => false;

        public bool Publish(string topic, IReadOnlyList<Message> parts, SendFlags flags) => false;

        public bool SendToSpot(RoutingId targetRid, RoutingId spotRid, Message message, SendFlags flags) => false;

        public bool SendToSpot(
            RoutingId targetRid,
            RoutingId spotRid,
            IReadOnlyList<Message> parts,
            SendFlags flags) => false;

        public bool RequestToSpot(
            RoutingId targetRid,
            RoutingId spotRid,
            Message message,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout) => false;

        public bool RequestToSpot(
            RoutingId targetRid,
            RoutingId spotRid,
            IReadOnlyList<Message> parts,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout) => false;

        public ZLinkBackendActorJoinRequest? RecvActorJoin(RecvFlags flags) => null;

        public ZLinkBackendSpotActorLifecycleEvent? RecvActorLifecycle(RecvFlags flags) => null;

        public void ReplyActorJoin(
            ZLinkBackendActorJoinRequest request,
            int joinResultCode,
            Message reply) { }

        public void ReplyActorJoin(
            ZLinkBackendActorJoinRequest request,
            int joinResultCode,
            IReadOnlyList<Message> parts) { }

        public void OnActorLifecycle(
            Action<ZLinkBackendSpotActorLifecycleInfo>? onJoin,
            Action<ZLinkBackendSpotActorLifecycleInfo>? onLeave) { }
    }

    private sealed class CapturingSpotNode : IZLinkBackendSpotNode
    {
        private readonly CapturingSpot _entrySpot = new();

        public List<CapturedActorReply> NoBindReplies { get; } = [];

        public List<(ZLinkBackendActorRef Actor, IReadOnlyList<byte[]> Parts)> BoundSessionReplies { get; } = [];

        public object NativeInstance => this;

        public RoutingId RoutingId { get; private set; }

        public ValueTask DisposeAsync() => ValueTask.CompletedTask;

        public void SetRoutingId(RoutingId routingId)
        {
            RoutingId = routingId;
        }

        public void SetPublisherRoutingId(RoutingId routingId) { }

        public void SetSubscriberRoutingId(RoutingId routingId) { }

        public void SetRouterBind(string endpoint) { }

        public void SetPubBind(string endpoint) { }

        public void ConnectPeer(string endpoint) { }

        public void ConnectPeer(RoutingId peerRid, string endpoint) { }

        public void DisconnectPeer(string endpoint) { }

        public IZLinkBackendSpot CreateSpot() => new CapturingSpot();

        public IZLinkBackendSpot GetOrCreateSpot(RoutingId spotRid, out bool created)
        {
            created = true;
            return new CapturingSpot();
        }

        public ZLinkSpotNodeStatus Status() => new(
            "entry",
            "inproc://entry",
            RoutingId,
            ZLinkSpotNodeState.Ready,
            0,
            0,
            0,
            0,
            0,
            0,
            0);

        public IReadOnlyList<ZLinkSpotNodePeerEntry> Peers() => [];

        public IReadOnlyList<ZLinkSpotNodeSubjectEntry> Subjects() => [];

        public IZLinkBackendSpotRouteBridge CreateRouteBridge() => throw new NotSupportedException();

        public IZLinkBackendSpot EntrySpot() => _entrySpot;

        public ZLinkBackendActorRef CreateActor(string actorId, Message createRequest)
        {
            return new ZLinkBackendActorRef(RoutingId, actorId, 1);
        }

        public ZLinkBackendActorRef? ActorLookup(string actorId) => null;

        public bool JoinActor(
            ZLinkBackendActorRef actor,
            RoutingId destNodeRid,
            RoutingId destSpotRid,
            Message message,
            RequestCallback callback,
            TimeSpan? timeout) => false;

        public bool JoinActor(
            ZLinkBackendActorRef actor,
            RoutingId destNodeRid,
            RoutingId destSpotRid,
            IReadOnlyList<Message> parts,
            ActorJoinCallback callback,
            TimeSpan? timeout) => false;

        public bool JoinActorEntrySpot(
            ZLinkBackendActorRef actor,
            RoutingId destNodeRid,
            Message request,
            ActorJoinEntrySpotCallback callback,
            TimeSpan? timeout) => false;

        public ValueTask DestroyActorAsync(
            ZLinkBackendActorRef actor,
            TimeSpan timeout,
            CancellationToken cancellationToken) => ValueTask.CompletedTask;

        public bool SendActorBoundSession(
            ZLinkBackendActorRef actor,
            IReadOnlyList<Message> parts,
            SendFlags flags)
        {
            BoundSessionReplies.Add((actor, CopyParts(parts)));
            return true;
        }

        public bool SendToActor(
            ZLinkBackendActorRef actor,
            IReadOnlyList<Message> parts,
            SendFlags flags) => false;

        public ValueTask<IReadOnlyList<Message>> RequestToActorAsync(
            ZLinkBackendActorRef actor,
            IReadOnlyList<Message> parts,
            TimeSpan? timeout,
            CancellationToken cancellationToken) => throw new NotSupportedException();

        public void ReplyActorNoBind(
            ZLinkBackendActorRef actor,
            RoutingId sourceNodeRid,
            RoutingId sourceSessionRid,
            ulong requestId,
            uint flags,
            IReadOnlyList<Message> parts)
        {
            NoBindReplies.Add(new CapturedActorReply(
                actor,
                sourceNodeRid,
                sourceSessionRid,
                requestId,
                flags,
                CopyParts(parts)));
        }

        public bool ForwardActorBoundSessionPart(
            ZLinkBackendActorRef actor,
            RoutingId sourceNodeRid,
            RoutingId sourceSessionRid,
            Message message,
            bool hasMore,
            SendFlags flags) => false;

        public void BindRemoteActorBoundSession(
            ZLinkBackendActorRef actor,
            RoutingId sourceNodeRid,
            RoutingId sourceSessionRid) { }

        public ValueTask CloseActorBoundSessionAsync(
            ZLinkBackendActorRef actor,
            TimeSpan timeout,
            CancellationToken cancellationToken) => ValueTask.CompletedTask;

        private static IReadOnlyList<byte[]> CopyParts(IReadOnlyList<Message> parts)
        {
            return parts.Select(static part => part.AsReadOnlySpan().ToArray()).ToArray();
        }
    }

    private sealed class CapturingBackendAdapterFactory(CapturingSpotNode node) : IZLinkBackendAdapterFactory
    {
        private readonly CapturingChannelBackendAdapter _channelAdapter = new();

        public IZLinkChannelBackendAdapter CreateChannelAdapter() => _channelAdapter;

        public IZLinkSpotBackendAdapter CreateSpotAdapter() => new CapturingSpotBackendAdapter(node);

        public IZLinkStreamBackendAdapter CreateStreamAdapter() => throw new NotSupportedException();

        public IZLinkMonitoringBackendAdapter CreateMonitoringAdapter() => throw new NotSupportedException();
    }

    private sealed class CapturingChannelBackendAdapter : IZLinkChannelBackendAdapter
    {
        public IZLinkBackendContext CreateContext() => new CapturingBackendContext();

        public IZLinkBackendDealerSocket CreateDealerSocket(IZLinkBackendContext context) =>
            throw new NotSupportedException();

        public IZLinkBackendRouterSocket CreateRouterSocket(IZLinkBackendContext context) =>
            throw new NotSupportedException();

        public IZLinkBackendPublisherSocket CreatePublisherSocket(IZLinkBackendContext context) =>
            throw new NotSupportedException();

        public IZLinkBackendSubscriberSocket CreateSubscriberSocket(IZLinkBackendContext context) =>
            throw new NotSupportedException();
    }

    private sealed class CapturingSpotBackendAdapter(CapturingSpotNode node) : IZLinkSpotBackendAdapter
    {
        public IZLinkBackendSpotNode CreateSpotNode(
            IZLinkBackendContext context,
            SpotNodeMode mode) => node;
    }

    private sealed class CapturingBackendContext : IZLinkBackendContext
    {
        public object NativeInstance => this;

        public ValueTask DisposeAsync() => ValueTask.CompletedTask;

        public void Shutdown() { }
    }

    private sealed class CapturingMessageFlowObserver : IZLinkMessageFlowObserver
    {
        private readonly TaskCompletionSource<ZLinkMessageFlowEvent> _observed =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public ValueTask OnMessageFlowAsync(
            ZLinkMessageFlowEvent flow,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            _observed.TrySetResult(flow);
            return ValueTask.CompletedTask;
        }

        public async Task<ZLinkMessageFlowEvent> WaitAsync(TimeSpan timeout)
        {
            return await _observed.Task.WaitAsync(timeout);
        }
    }

    private sealed class ThrowingBackendAdapterFactory : IZLinkBackendAdapterFactory
    {
        public IZLinkChannelBackendAdapter CreateChannelAdapter() => throw new NotSupportedException();

        public IZLinkSpotBackendAdapter CreateSpotAdapter() => throw new NotSupportedException();

        public IZLinkStreamBackendAdapter CreateStreamAdapter() => throw new NotSupportedException();

        public IZLinkMonitoringBackendAdapter CreateMonitoringAdapter() => throw new NotSupportedException();
    }

    private sealed class ThrowingRuntimeErrorSink : IZLinkRuntimeErrorSink
    {
        public void ReportHandlerException(Exception exception)
        {
            throw new InvalidOperationException("Runtime handler failed.", exception);
        }

        public void ReportRuntimeTaskException(string name, Exception exception)
        {
            throw new InvalidOperationException($"Runtime task '{name}' failed.", exception);
        }
    }
}
