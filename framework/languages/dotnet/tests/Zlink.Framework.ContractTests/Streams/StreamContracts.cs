using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.ContractTests.Support;

namespace Zlink.Framework.ContractTests.Streams;

public sealed class StreamContracts
{
    [Fact]
    [ContractExample(
        typeof(IZLinkSession),
        typeof(IZLinkSessionContext),
        typeof(IZLinkSessionIdentityContext),
        typeof(IZLinkSessionClientStream),
        typeof(IZLinkSessionActorDispatchContext),
        typeof(IZLinkSessionLifecycle),
        typeof(IZLinkSessionActorAttachmentContext),
        typeof(IZLinkSessionSendCall),
        typeof(IZLinkSessionReplyCall),
        typeof(IZLinkSessionPacketHandler<>),
        typeof(IZLinkSessionPacketDispatcher<>),
        typeof(IZLinkActorRef),
        typeof(IZLinkStream))]
    public async Task Session_context_collects_identity_stream_and_actor_operations()
    {
        var context = new ExampleSessionContext();
        var session = new ExampleSession(context);
        var actor = new ExampleActor("player-1");

        await session.OnConnectedAsync(CancellationToken.None);
        await context.AttachActorAsync(actor);
        var actorRef = await context.BindActorHandleAsync("player-1", "player");
        var foundActor = context.TryGetBoundActor("player-1", out var boundActor);
        await context.RelayToActorAsync(
            actorRef,
            new ZlinkStreamHeader(
                ZlinkStreamMessageKind.Send,
                ZlinkStreamCodec.Json,
                ZlinkStreamHeaderFlags.None,
                null,
                "player.joined",
                ZlinkStreamMetadata.Empty),
            new Message());
        await context.NotifyActorDisconnectedAsync(actorRef);

        await context
            .Send(new PlayerJoined("player-1"))
            .PacketName("player.joined")
            .Metadata("trace-id", "abc")
            .Compress()
            .Submit();

        await context
            .Reply(new AuthenticateReply("player-1"))
            .Metadata("trace-id", "abc")
            .Compress()
            .Submit();

        await context.CloseAsync();

        IZLinkStream stream = context;
        stream.Write(new Message());
        await stream.CloseAsync();

        Assert.Equal("session-1", session.Context.SessionId);
        Assert.Equal("player-1", actorRef.ActorId);
        Assert.True(foundActor);
        Assert.Same(actorRef, boundActor);
        Assert.True(context.IsClosed);
        Assert.True(context.StreamClosed);
    }

    [Fact]
    [ContractExample(
        typeof(IZLinkSessionPacketHandler<>),
        typeof(IZLinkSessionPacketDispatcher<>))]
    public async Task Session_packet_dispatcher_handles_only_registered_packets()
    {
        var sessionContext = new SessionPacketContext();
        IZLinkSessionPacketDispatcher<SessionPacketContext> dispatcher =
            new ExampleSessionPacketDispatcher<SessionPacketContext>(
            [
                new AuthenticatePacketHandler()
            ]);

        var handled = await dispatcher.TryHandleAsync(
            sessionContext,
            new ZlinkStreamHeader(
                ZlinkStreamMessageKind.Send,
                ZlinkStreamCodec.Json,
                ZlinkStreamHeaderFlags.None,
                null,
                "auth",
                ZlinkStreamMetadata.Empty),
            new Message());
        var unhandled = await dispatcher.TryHandleAsync(
            sessionContext,
            new ZlinkStreamHeader(
                ZlinkStreamMessageKind.Send,
                ZlinkStreamCodec.Json,
                ZlinkStreamHeaderFlags.None,
                null,
                "gameplay",
                ZlinkStreamMetadata.Empty),
            new Message());

        Assert.True(handled);
        Assert.False(unhandled);
        Assert.Equal("auth", sessionContext.LastPacketName);
    }

    [Fact]
    [ContractExample(
        typeof(IZLinkBoundSession),
        typeof(IZLinkBoundSessionSendCall),
        typeof(IZLinkMessageMetadataPolicy))]
    public async Task Bound_session_sends_to_the_bound_session_without_exposing_stream_transport()
    {
        var boundSession = new ExampleBoundSession();

        await boundSession
            .Send(new PlayerJoined("player-1"))
            .PacketName("player.joined")
            .Metadata("trace-id", "abc")
            .Submit();

        await boundSession.DisconnectAsync();

        IZLinkMessageMetadataPolicy policy = new MetadataPolicy(
            new HashSet<string>(StringComparer.Ordinal) { "trace-id" });

        Assert.True(boundSession.IsDisconnected);
        Assert.True(policy.CanForwardApplicationKey("trace-id"));
        Assert.False(policy.CanForwardApplicationKey("internal-key"));
    }

    private sealed record PlayerJoined(string PlayerId);

    private sealed record AuthenticateReply(string PlayerId);

    private sealed class SessionPacketContext
    {
        public string? LastPacketName { get; set; }
    }

    private sealed class AuthenticatePacketHandler : IZLinkSessionPacketHandler<SessionPacketContext>
    {
        public string PacketName => "auth";

        public ValueTask HandleAsync(
            SessionPacketContext context,
            ZlinkStreamHeader header,
            Message payload,
            CancellationToken cancellationToken)
        {
            _ = payload;
            cancellationToken.ThrowIfCancellationRequested();
            context.LastPacketName = header.Name;
            return ValueTask.CompletedTask;
        }
    }

    private sealed class ExampleSessionPacketDispatcher<TContext>(
        IEnumerable<IZLinkSessionPacketHandler<TContext>> handlers)
        : IZLinkSessionPacketDispatcher<TContext>
    {
        private readonly IReadOnlyDictionary<string, IZLinkSessionPacketHandler<TContext>> _handlers =
            handlers.ToDictionary(static handler => handler.PacketName, StringComparer.Ordinal);

        public async ValueTask<bool> TryHandleAsync(
            TContext context,
            ZlinkStreamHeader header,
            Message payload,
            CancellationToken cancellationToken = default)
        {
            if (!_handlers.TryGetValue(header.Name, out var handler))
            {
                return false;
            }

            await handler.HandleAsync(context, header, payload, cancellationToken);
            return true;
        }
    }

    private sealed class ExampleSession(IZLinkSessionContext context) : IZLinkSession
    {
        public IZLinkSessionContext Context { get; } = context;

        public ValueTask OnConnectedAsync(CancellationToken cancellationToken) => ValueTask.CompletedTask;

        public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken) => ValueTask.CompletedTask;

        public ValueTask OnErrorAsync(
            ZLinkStreamError error,
            CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;
    }

    private sealed class ExampleSessionContext :
        IZLinkSessionContext,
        IZLinkSessionActorAttachmentContext,
        IZLinkStream
    {
        public string SessionId => "session-1";

        public RoutingId? RoutingId => Systems.Zlink.RoutingId.Of("session-route");

        public string? LocalAddr => "tcp://127.0.0.1:5000";

        public string? RemoteAddr => "tcp://127.0.0.1:5001";

        private readonly Dictionary<string, IZLinkActorRef> _actors = new(StringComparer.Ordinal);

        public IReadOnlyCollection<IZLinkActorRef> BoundActors => _actors.Values.ToArray();

        public bool IsClosed { get; private set; }

        public bool StreamClosed { get; private set; }

        public IZLinkSessionSendCall Send<TMessage>(TMessage message) => new SessionSendCall();

        public IZLinkSessionReplyCall Reply<TMessage>(TMessage message) => new SessionReplyCall();

        public ValueTask<IZLinkActorRef> BindActorHandleAsync(
            string actorId,
            string actorType,
            CancellationToken cancellationToken = default)
        {
            var actor = new ActorRef(actorId, actorType);
            _actors[actorId] = actor;
            return ValueTask.FromResult<IZLinkActorRef>(actor);
        }

        public ValueTask<IZLinkActorRef> BindActorHandleAsync(
            string actorId,
            string actorType,
            ZLinkActorRemoteAddress remoteAddress,
            CancellationToken cancellationToken = default)
        {
            var actor = new ActorRef(
                actorId,
                actorType,
                true,
                remoteAddress,
                new Systems.Zlink.ActorRef(
                    remoteAddress.TargetNodeRid,
                    actorId,
                    remoteAddress.ActorGeneration));
            _actors[actorId] = actor;
            return ValueTask.FromResult<IZLinkActorRef>(actor);
        }

        public ValueTask<IZLinkActorRef> BindActorHandleAsync(
            Systems.Zlink.ActorRef actorRef,
            string actorType,
            CancellationToken cancellationToken = default)
        {
            var actor = new ActorRef(
                actorRef.ActorId,
                actorType,
                true,
                new ZLinkActorRemoteAddress(string.Empty, actorRef.NodeRid, actorRef.Generation),
                actorRef);
            _actors[actorRef.ActorId] = actor;
            return ValueTask.FromResult<IZLinkActorRef>(actor);
        }

        public ValueTask<IZLinkActorRef> BindActorHandleAsync(
            IZLinkActorRef actor,
            CancellationToken cancellationToken = default)
        {
            _actors[actor.ActorId] = actor;
            return ValueTask.FromResult(actor);
        }

        public bool TryGetBoundActor(
            string actorId,
            out IZLinkActorRef actor)
        {
            return _actors.TryGetValue(actorId, out actor!);
        }

        public ValueTask RelayToActorAsync(
            IZLinkActorRef actor,
            ZlinkStreamHeader header,
            Message payload,
            CancellationToken cancellationToken = default) =>
            ValueTask.CompletedTask;

        public ValueTask NotifyActorDisconnectedAsync(
            IZLinkActorRef actor,
            CancellationToken cancellationToken = default) =>
            ValueTask.CompletedTask;

        public ValueTask CloseAsync(CancellationToken cancellationToken = default)
        {
            IsClosed = true;
            return ValueTask.CompletedTask;
        }

        public ValueTask AttachActorAsync(
            IZLinkActor actor,
            CancellationToken cancellationToken = default) =>
            ValueTask.CompletedTask;

        bool IZLinkStream.Write(Message payload, SendFlags flags) => true;

        async ValueTask IZLinkStream.CloseAsync(CancellationToken cancellationToken)
        {
            StreamClosed = true;
            await CloseAsync(cancellationToken);
        }
    }

    private sealed class ExampleBoundSession : IZLinkBoundSession
    {
        public bool IsDisconnected { get; private set; }

        public IZLinkBoundSessionSendCall Send<TMessage>(TMessage message) => new BoundSessionSendCall();

        public ValueTask DisconnectAsync(CancellationToken cancellationToken = default)
        {
            IsDisconnected = true;
            return ValueTask.CompletedTask;
        }
    }

    private sealed class ActorRef(
        string actorId,
        string actorType,
        bool isRemote = false,
        ZLinkActorRemoteAddress remoteAddress = default,
        Systems.Zlink.ActorRef actor = default) : IZLinkActorRef
    {
        public string ActorId { get; } = actorId;

        public string ActorType { get; } = actorType;

        public Systems.Zlink.ActorRef Actor { get; } = actor.Equals(default)
            ? new Systems.Zlink.ActorRef(
                remoteAddress.TargetNodeRid.IsEmpty ? RoutingId.Of("actor-node") : remoteAddress.TargetNodeRid,
                actorId,
                remoteAddress.ActorGeneration == 0 ? 1 : remoteAddress.ActorGeneration)
            : actor;

        public bool IsRemote { get; } = isRemote;

        public ZLinkActorRemoteAddress RemoteAddress { get; } = remoteAddress;
    }

    private sealed class ExampleActor(string actorId) : IZLinkActor
    {
        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context => null!;
    }

    private sealed class SendCall : IZLinkSendCall
    {
        public IZLinkSendCall PacketName(string messageName) => this;

        public ValueTask Submit(CancellationToken cancellationToken = default) => ValueTask.CompletedTask;
    }

    private sealed class RequestCall(object reply) : IZLinkRequestCall
    {
        public IZLinkRequestCall PacketName(string messageName) => this;

        public IZLinkRequestCall Timeout(TimeSpan timeout) => this;

        public ValueTask<TReply> SubmitAsync<TReply>(CancellationToken cancellationToken = default) =>
            ValueTask.FromResult((TReply)reply);
    }

    private sealed class SessionSendCall : IZLinkSessionSendCall
    {
        public IZLinkSessionSendCall Metadata(string key, string value) => this;

        public IZLinkSessionSendCall PacketName(string messageName) => this;

        public IZLinkSessionSendCall Compress() => this;

        public ValueTask Submit(CancellationToken cancellationToken = default) => ValueTask.CompletedTask;
    }

    private sealed class SessionReplyCall : IZLinkSessionReplyCall
    {
        public IZLinkSessionReplyCall Metadata(string key, string value) => this;

        public IZLinkSessionReplyCall Compress() => this;

        public ValueTask Submit(CancellationToken cancellationToken = default) => ValueTask.CompletedTask;
    }

    private sealed class BoundSessionSendCall : IZLinkBoundSessionSendCall
    {
        public IZLinkBoundSessionSendCall PacketName(string packetName) => this;

        public IZLinkBoundSessionSendCall Metadata(string key, string value) => this;

        public ValueTask Submit(CancellationToken cancellationToken = default) => ValueTask.CompletedTask;
    }

    private sealed class MetadataPolicy(IReadOnlySet<string> forwardedKeys) : IZLinkMessageMetadataPolicy
    {
        public bool CanForwardApplicationKey(string key) => forwardedKeys.Contains(key);
    }
}
