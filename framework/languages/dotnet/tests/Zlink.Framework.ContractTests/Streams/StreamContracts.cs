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
        Assert.True(context.IsClosed);
        Assert.True(context.StreamClosed);
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

        public bool IsClosed { get; private set; }

        public bool StreamClosed { get; private set; }

        public IZLinkSessionSendCall Send<TMessage>(TMessage message) => new SessionSendCall();

        public IZLinkSessionReplyCall Reply<TMessage>(TMessage message) => new SessionReplyCall();

        public ValueTask<IZLinkActorRef> BindActorHandleAsync(
            string actorId,
            string actorType,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult<IZLinkActorRef>(new ActorRef(actorId, actorType));

        public ValueTask<IZLinkActorRef> BindActorHandleAsync(
            string actorId,
            string actorType,
            ZLinkActorRemoteAddress remoteAddress,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult<IZLinkActorRef>(new ActorRef(actorId, actorType, true, remoteAddress));

        public ValueTask<IZLinkActorRef> BindActorHandleAsync(
            IZLinkActorRef actor,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(actor);

        public ValueTask RelayToActorAsync(
            IZLinkActorRef actor,
            ZlinkStreamHeader header,
            Message payload,
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
        ZLinkActorRemoteAddress remoteAddress = default) : IZLinkActorRef
    {
        public string ActorId { get; } = actorId;

        public string ActorType { get; } = actorType;

        public bool IsRemote { get; } = isRemote;

        public ZLinkActorRemoteAddress RemoteAddress { get; } = remoteAddress;
    }

    private sealed class ExampleActor(string actorId) : IZLinkActor
    {
        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context => null!;

        public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken) => ValueTask.CompletedTask;
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
