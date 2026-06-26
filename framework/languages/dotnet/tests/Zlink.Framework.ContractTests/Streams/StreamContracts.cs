using Zlink.Framework.ContractTests.Support;

namespace Zlink.Framework.ContractTests.Streams;

public sealed class StreamContracts
{
    [Fact]
    [ContractExample(
        typeof(IZLinkSession),
        typeof(IZLinkSessionContext),
        typeof(IZLinkSessionClient),
        typeof(IZLinkSessionActors),
        typeof(IZLinkSessionSendCall),
        typeof(IZLinkSessionReplyCall),
        typeof(IZLinkSessionPacketHandler<>),
        typeof(IZLinkSessionPacketDispatcher<>),
        typeof(IZLinkSessionActor),
        typeof(IZLinkStream))]
    public async Task Session_context_collects_identity_stream_and_actor_operations()
    {
        var context = new ExampleSessionContext();
        var session = new ExampleSession(context);

        await session.OnConnectedAsync(CancellationToken.None);
        var actorRef = await context.Actors.BindAsync(new Systems.Zlink.ActorRef(RoutingId.From("actor-node"), "player-1", 1));
        var boundActor = context.Actors.Find("player-1");
        await actorRef.RelayAsync(
            Zlink.Framework.Contracts.Messaging.ZLinkMessage.From(new PlayerJoined("player-1")));
        await actorRef.NotifyDisconnectedAsync();

        await context.Client
            .Send(new PlayerJoined("player-1"))
            .PacketName("player.joined")
            .Metadata("trace-id", "abc")
            .Compress()
            .Async();

        await context
            .Client.Reply(new AuthenticateReply("player-1"))
            .Metadata("trace-id", "abc")
            .Compress()
            .Async();

        await context.CloseAsync();

        IZLinkStream stream = context;
        stream.Write(Zlink.Framework.Contracts.Messaging.ZLinkMessage.From(new AuthenticateReply("token")));
        await stream.CloseAsync();

        Assert.Equal("session-1", session.Context.SessionId);
        Assert.Equal("player-1", actorRef.ActorId);
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
            new ZLinkSessionDispatchContext("auth"),
            Zlink.Framework.Contracts.Messaging.ZLinkMessage.From(new AuthenticateReply("token")));
        var unhandled = await dispatcher.TryHandleAsync(
            sessionContext,
            new ZLinkSessionDispatchContext("gameplay"),
            Zlink.Framework.Contracts.Messaging.ZLinkMessage.From(new PlayerJoined("player-1")));

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
            .Async();

        await boundSession
            .Send(new PlayerJoined("player-2"))
            .PacketName("player.joined")
            .Async();

        await boundSession.DisconnectAsync();

        IZLinkMessageMetadataPolicy policy = new MetadataPolicy(
            new HashSet<string>(StringComparer.Ordinal) { "trace-id" });
        var metadata = new ZLinkMessageMetadata(new Dictionary<string, string>(StringComparer.Ordinal)
        {
            ["trace-id"] = "abc"
        });

        Assert.True(boundSession.IsDisconnected);
        Assert.True(policy.CanForward("trace-id"));
        Assert.False(policy.CanForward("internal-key"));
        Assert.Equal("abc", metadata.Find("trace-id"));
        Assert.Null(metadata.Find("tenant-id"));
        Assert.Null(typeof(ZLinkMessageMetadata).GetProperty("Application"));
        Assert.Null(typeof(ZLinkMessageMetadata).GetProperty("Codec"));
        Assert.Null(typeof(ZLinkMessageMetadata).GetMethod("TryGetApplicationValue"));
        Assert.Null(typeof(ZLinkMessageMetadata).GetMethod("TryGetCodecValue"));
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
            ZLinkSessionDispatchContext dispatch,
            Zlink.Framework.Contracts.Messaging.ZLinkMessage payload,
            CancellationToken cancellationToken)
        {
            _ = payload;
            cancellationToken.ThrowIfCancellationRequested();
            context.LastPacketName = dispatch.PacketName;
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
            ZLinkSessionDispatchContext dispatch,
            Zlink.Framework.Contracts.Messaging.ZLinkMessage payload,
            CancellationToken cancellationToken = default)
        {
            if (!_handlers.TryGetValue(dispatch.PacketName, out var handler))
            {
                return false;
            }

            await handler.HandleAsync(context, dispatch, payload, cancellationToken);
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
        IZLinkSessionClient,
        IZLinkSessionActors,
        IZLinkStream
    {
        public string SessionId => "session-1";

        public RoutingId? RoutingId => Systems.Zlink.RoutingId.From("session-route");

        public string? LocalAddr => "tcp://127.0.0.1:5000";

        public string? RemoteAddr => "tcp://127.0.0.1:5001";

        private readonly Dictionary<string, IZLinkSessionActor> _actors = new(StringComparer.Ordinal);

        public IZLinkSessionClient Client => this;

        public IZLinkSessionActors Actors => this;

        public IReadOnlyCollection<IZLinkSessionActor> Bound => _actors.Values.ToArray();

        public bool IsClosed { get; private set; }

        public bool StreamClosed { get; private set; }

        public IZLinkSessionSendCall Send<TMessage>(TMessage message) => new SessionSendCall();

        public IZLinkSessionReplyCall Reply<TMessage>(TMessage message) => new SessionReplyCall();

        public ValueTask<IZLinkSessionActor> BindAsync(
            IZLinkActor actor,
            CancellationToken cancellationToken = default)
        {
            return BindAsync(
                new Systems.Zlink.ActorRef(Systems.Zlink.RoutingId.From("actor-node"), actor.ActorId, 1),
                cancellationToken);
        }

        public ValueTask<IZLinkSessionActor> BindAsync(
            Systems.Zlink.ActorRef actorRef,
            CancellationToken cancellationToken = default)
        {
            var actor = new ActorRef(actorRef);
            _actors[actorRef.ActorId] = actor;
            return ValueTask.FromResult<IZLinkSessionActor>(actor);
        }

        public IZLinkSessionActor? Find(string actorId)
        {
            return _actors.GetValueOrDefault(actorId);
        }

        public ValueTask CloseAsync()
        {
            IsClosed = true;
            return ValueTask.CompletedTask;
        }

        bool IZLinkStream.Write(
            Zlink.Framework.Contracts.Messaging.ZLinkMessage payload,
            SendFlags flags) => true;

        async ValueTask IZLinkStream.CloseAsync()
        {
            StreamClosed = true;
            await CloseAsync();
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

    private sealed class ActorRef(Systems.Zlink.ActorRef actor) : IZLinkSessionActor
    {
        public string ActorId => Ref.ActorId;

        public Systems.Zlink.ActorRef Ref { get; } = actor;

        public ValueTask RelayAsync(
            Zlink.Framework.Contracts.Messaging.ZLinkMessage payload,
            CancellationToken cancellationToken = default) =>
            ValueTask.CompletedTask;

        public ValueTask NotifyDisconnectedAsync(CancellationToken cancellationToken = default) =>
            ValueTask.CompletedTask;
    }

    private sealed class SendCall : IZLinkSendCall
    {
        public IZLinkSendCall PacketName(string messageName) => this;

        public ValueTask Async(CancellationToken cancellationToken = default) => ValueTask.CompletedTask;
    }

    private sealed class RequestCall(object reply) : IZLinkRequestCall
    {
        public IZLinkRequestCall PacketName(string messageName) => this;

        public IZLinkRequestCall Timeout(TimeSpan timeout) => this;

        public ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default) =>
            ValueTask.FromResult((TReply)reply);
    }

    private sealed class SessionSendCall : IZLinkSessionSendCall
    {
        public IZLinkSessionSendCall Metadata(string key, string value) => this;

        public IZLinkSessionSendCall PacketName(string messageName) => this;

        public IZLinkSessionSendCall Compress() => this;

        public ValueTask Async() => ValueTask.CompletedTask;
    }

    private sealed class SessionReplyCall : IZLinkSessionReplyCall
    {
        public IZLinkSessionReplyCall Metadata(string key, string value) => this;

        public IZLinkSessionReplyCall Compress() => this;

        public ValueTask Async() => ValueTask.CompletedTask;
    }

    private sealed class BoundSessionSendCall : IZLinkBoundSessionSendCall
    {
        public IZLinkBoundSessionSendCall PacketName(string packetName) => this;

        public IZLinkBoundSessionSendCall Metadata(string key, string value) => this;

        public ValueTask Async(CancellationToken cancellationToken = default) => ValueTask.CompletedTask;

    }

    private sealed class MetadataPolicy(IReadOnlySet<string> forwardedKeys) : IZLinkMessageMetadataPolicy
    {
        public bool CanForward(string key) => forwardedKeys.Contains(key);
    }
}
