using Microsoft.Extensions.DependencyInjection;
using System.Diagnostics.Metrics;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.Contracts.Messaging;

namespace Zlink.Framework.UnitTests;

[Collection(RuntimeMetricsCollection.Name)]
public sealed class SessionActorCoordinatorTests
{
    [Fact]
    public async Task Session_Send_Submit_Reports_Nonblocking_Transport_Backpressure()
    {
        var runtime = CreateRuntime();
        var stream = new TestStream(RoutingId.From("session-node"), acceptsWrites: false);
        var context = new ZLinkSessionContext(
            runtime,
            stream,
            new TestSessionHandlerRegistry(),
            static () => ValueTask.CompletedTask,
            static _ => ValueTask.CompletedTask);

        var result = await context.Client.Send(new SessionPush("value")).SubmitAsync();

        Assert.Equal(ZLinkSubmitStatus.Backpressured, result.Status);
        Assert.Equal(SendFlags.DontWait, stream.LastWriteFlags);
    }

    [Fact]
    public async Task Session_Reply_PreCancellation_Claims_The_Reply_Token_Before_Admission()
    {
        var runtime = CreateRuntime();
        var stream = new TestStream(RoutingId.From("session-node"));
        var context = new ZLinkSessionContext(
            runtime,
            stream,
            new TestSessionHandlerRegistry(),
            static () => ValueTask.CompletedTask,
            static _ => ValueTask.CompletedTask);
        _ = context.EnterDispatch(new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Request,
            ZlinkStreamCodec.Json,
            ZlinkStreamHeaderFlags.HasRequestSeq,
            new ZlinkStreamRequestSeq(1),
            "SessionRequest",
            ZlinkStreamMetadata.Empty));
        using var cancellation = new CancellationTokenSource();
        cancellation.Cancel();

        await Assert.ThrowsAnyAsync<OperationCanceledException>(() =>
            context.Client.Reply(new SessionPush("cancelled"))
                .SubmitAsync(cancellation.Token)
                .AsTask());
        Assert.Empty(stream.Writes);

        await Assert.ThrowsAsync<InvalidOperationException>(() =>
            context.Client.Reply(new SessionPush("duplicate"))
                .SubmitAsync()
                .AsTask());
    }

    [Fact]
    public async Task BindOrGetActorAsync_Rebinds_When_Generation_Changes_For_Same_ActorId()
    {
        var bindDurations = new List<double>();
        Instrument? bindDurationInstrument = null;
        using var listener = new MeterListener
        {
            InstrumentPublished = (instrument, owner) =>
            {
                if (instrument.Meter.Name == ZLinkMeters.Framework
                    && instrument.Name == "zlink.stream.session.bind.duration")
                {
                    bindDurationInstrument = instrument;
                    owner.EnableMeasurementEvents(instrument);
                }
            }
        };
        listener.SetMeasurementEventCallback<double>((_, value, _, _) => bindDurations.Add(value));
        listener.Start();
        var runtime = CreateRuntime();
        var context = new ZLinkSessionContext(
            runtime,
            new TestStream(RoutingId.From("session-node")),
            new TestSessionHandlerRegistry(),
            static () => ValueTask.CompletedTask,
            static _ => ValueTask.CompletedTask);

        var firstRef = new ActorRef(RoutingId.From("actor-node"), "actor-1", 1);
        var secondRef = new ActorRef(RoutingId.From("actor-node"), "actor-1", 2);

        var first = await context.ActorCoordinator.BindOrGetActorAsync(
            context,
            firstRef,
            CancellationToken.None);
        Assert.True(runtime.TryGetActorBoundSession("actor-1", out var firstSession));
        var firstToken = firstSession.BindingToken;

        var second = await context.ActorCoordinator.BindOrGetActorAsync(
            context,
            secondRef,
            CancellationToken.None);

        Assert.NotSame(first, second);
        Assert.Equal(secondRef, second.Ref);
        Assert.Single(context.Actors.Bound);
        Assert.DoesNotContain(first, context.Actors.Bound);
        Assert.False(runtime.TryGetSessionActorContext("actor-1", firstToken, out _));
        Assert.True(runtime.TryGetActorBoundSession("actor-1", out var secondSession));
        Assert.NotEqual(firstToken, secondSession.BindingToken);
        Assert.True(runtime.TryGetSessionActorContext("actor-1", secondSession.BindingToken, out var reboundContext));
        Assert.Same(context, reboundContext);
        Assert.Equal(2, bindDurations.Count);
        Assert.All(bindDurations, duration => Assert.True(duration >= 0));
        Assert.NotNull(bindDurationInstrument);
        listener.DisableMeasurementEvents(bindDurationInstrument);
    }

    [Fact]
    public async Task Local_Actor_Bound_Session_Send_Uses_The_Bound_Stream()
    {
        var runtime = CreateRuntime();
        var stream = new TestStream(RoutingId.From("session-node"));
        var context = new ZLinkSessionContext(
            runtime,
            stream,
            new TestSessionHandlerRegistry(),
            static () => ValueTask.CompletedTask,
            static _ => ValueTask.CompletedTask);
        var actor = new ActorRef(RoutingId.From("actor-node"), "actor-1", 1);
        await context.ActorCoordinator.BindOrGetActorAsync(context, actor, CancellationToken.None);

        using var payload = Message.From(new byte[] { 1, 2, 3 });
        Assert.True(runtime.SendActorBoundSession(
            actor.ActorId,
            new[] { payload },
            SendFlags.DontWait));

        var frame = Assert.Single(stream.Writes);
        Assert.Equal(SendFlags.DontWait, frame.Flags);
        Assert.NotEmpty(frame.Payload);
    }

    [Fact]
    public void Stale_Local_Actor_Binding_Does_Not_Fall_Through_To_Native_Routing()
    {
        var runtime = CreateRuntime();
        runtime.BindActorSession(
            "actor-1",
            RoutingId.From("session-node"),
            RoutingId.From("session-rid"),
            "local-binding-token");
        using var payload = Message.From(new byte[] { 1, 2, 3 });

        var exception = Assert.Throws<ZLinkFrameworkException>(() =>
            runtime.SendActorBoundSession(
                "actor-1",
                new[] { payload },
                SendFlags.DontWait));

        Assert.Equal(ZLinkFrameworkErrorKind.ActorSessionNotBound, exception.Kind);
    }

    [Fact]
    public void Bound_Session_Cleanup_Is_Isolated_Per_Runtime_When_Session_Rids_Match()
    {
        var runtimeA = CreateRuntime();
        var runtimeB = CreateRuntime();
        var sharedSessionRid = RoutingId.From("shared-session");
        var token = ZLinkActorBoundSessionBindingToken.Native(sharedSessionRid);
        runtimeA.BindActorSession("actor-a", null, sharedSessionRid, token);
        runtimeB.BindActorSession("actor-b", null, sharedSessionRid, token);

        Assert.True(runtimeA.TryGetActorBoundSession("actor-a", out _));
        Assert.True(runtimeB.TryGetActorBoundSession("actor-b", out _));

        runtimeA.CleanupActorSessionsForSession(sharedSessionRid);

        Assert.False(runtimeA.TryGetActorBoundSession("actor-a", out _));
        Assert.True(runtimeB.TryGetActorBoundSession("actor-b", out var remaining));
        Assert.Equal(sharedSessionRid, remaining.SessionRid);
    }

    [Fact]
    public async Task Stale_Session_Disconnect_Does_Not_Notify_Or_Clear_The_Replacement_Binding()
    {
        var runtime = CreateRuntime();
        var firstContext = CreateSessionContext(runtime, "session-old");
        var replacementContext = CreateSessionContext(runtime, "session-new");
        var actor = new ActorRef(RoutingId.From("actor-node"), "actor-1", 1);
        var oldBinding = await firstContext.ActorCoordinator.BindOrGetActorAsync(
            firstContext,
            actor,
            CancellationToken.None);
        _ = await replacementContext.ActorCoordinator.BindOrGetActorAsync(
            replacementContext,
            actor,
            CancellationToken.None);
        Assert.True(runtime.TryGetActorBoundSession(actor.ActorId, out var replacement));

        await oldBinding.NotifyDisconnectedAsync();

        Assert.True(runtime.TryGetActorBoundSession(actor.ActorId, out var current));
        Assert.Equal(replacement.BindingToken, current.BindingToken);
        Assert.True(runtime.TryGetSessionActorContext(
            actor.ActorId,
            current.BindingToken,
            out var currentContext));
        Assert.Same(replacementContext, currentContext);
    }

    [Fact]
    public async Task Bound_Actor_Relay_Rejects_A_Stale_Directory_Ref_Before_Native_Send()
    {
        var runtime = CreateRuntime(actorDirectory: new MissingActorDirectory());
        var context = CreateSessionContext(runtime, "session-rid");
        var actor = new ActorRef(RoutingId.From("actor-node"), "actor-1", 1);
        var bound = await context.ActorCoordinator.BindOrGetActorAsync(
            context,
            actor,
            CancellationToken.None);
        using var payload = Message.From(new byte[] { 1, 2, 3 });
        var header = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Request,
            ZlinkStreamCodec.Json,
            ZlinkStreamHeaderFlags.HasRequestSeq,
            new ZlinkStreamRequestSeq(1),
            "ActorPingReq",
            ZlinkStreamMetadata.Empty);

        var error = await Assert.ThrowsAsync<ZLinkFrameworkException>(async () =>
            await context.ActorCoordinator.RelayToActorAsync(
                bound,
                header,
                payload,
                static (_, _, _) => ValueTask.CompletedTask,
                CancellationToken.None));

        Assert.Equal(ZLinkFrameworkErrorKind.ActorRouteNotFound, error.Kind);
    }

    [Fact]
    public async Task Bound_Actor_Relay_Rebinds_To_The_Current_Directory_Ref_After_Handoff()
    {
        var current = new ActorRef(RoutingId.From("actor-node-b"), "actor-1", 2);
        var runtime = CreateRuntime(actorDirectory: new FixedActorDirectory(current));
        var context = CreateSessionContext(runtime, "session-rid");
        var stale = new ActorRef(RoutingId.From("actor-node-a"), "actor-1", 1);
        var bound = await context.ActorCoordinator.BindOrGetActorAsync(
            context,
            stale,
            CancellationToken.None);
        using var payload = Message.From(new byte[] { 1, 2, 3 });
        var header = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Send,
            ZlinkStreamCodec.Json,
            ZlinkStreamHeaderFlags.None,
            null,
            "ActorPingReq",
            ZlinkStreamMetadata.Empty);

        await Assert.ThrowsAnyAsync<Exception>(async () =>
            await context.ActorCoordinator.RelayToActorAsync(
                bound,
                header,
                payload,
                static (_, _, _) => ValueTask.CompletedTask,
                CancellationToken.None));

        var rebound = Assert.Single(context.Actors.Bound);
        Assert.Equal(current, rebound.Ref);
        Assert.DoesNotContain(bound, context.Actors.Bound);
    }

    private static ZLinkSessionContext CreateSessionContext(
        ZLinkFrameworkRuntime runtime,
        string sessionRid)
    {
        return new ZLinkSessionContext(
            runtime,
            new TestStream(RoutingId.From(sessionRid)),
            new TestSessionHandlerRegistry(),
            static () => ValueTask.CompletedTask,
            static _ => ValueTask.CompletedTask);
    }

    private static ZLinkFrameworkRuntime CreateRuntime(IZLinkActorDirectory? actorDirectory = null)
    {
        var registration = new ZLinkFrameworkRegistration();
        var services = new ServiceCollection();
        services.AddSingleton(registration);
        if (actorDirectory is not null) services.AddSingleton(actorDirectory);
        var provider = services.BuildServiceProvider();

        return new ZLinkFrameworkRuntime(
            provider,
            null!,
            registration,
            new ZLinkHandlerRegistry([]),
            new ZLinkHandlerDispatcher(
                provider.GetRequiredService<IServiceScopeFactory>(),
                registration));
    }

    private sealed record SessionPush(string Value);

    private sealed class MissingActorDirectory : IZLinkActorDirectory
    {
        public ValueTask<ActorRef?> FindAsync(
            string actorId,
            CancellationToken cancellationToken = default) => ValueTask.FromResult<ActorRef?>(null);

        public ValueTask<ActorRef> EnsureAsync(
            string actorId,
            ZLinkMessage createRequest,
            ZLinkActorPlacement placement = default,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();
    }

    private sealed class FixedActorDirectory(ActorRef actor) : IZLinkActorDirectory
    {
        public ValueTask<ActorRef?> FindAsync(
            string actorId,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult<ActorRef?>(actor.ActorId == actorId ? actor : null);

        public ValueTask<ActorRef> EnsureAsync(
            string actorId,
            ZLinkMessage createRequest,
            ZLinkActorPlacement placement = default,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();
    }

    private sealed class TestSessionHandlerRegistry : IZLinkSessionHandlerRegistry
    {
        public void AddHandler<THandler>() where THandler : class { }

        public void AddHandler<THandler>(string packetName) where THandler : class { }

        public ValueTask<bool> TryHandleAsync(
            ZLinkSessionDispatchContext dispatch,
            ZLinkMessage payload,
            CancellationToken cancellationToken = default) => ValueTask.FromResult(false);
    }

    private sealed class TestStream(RoutingId routingId, bool acceptsWrites = true) : IZLinkStream
    {
        public string SessionId { get; } = routingId.ToHex();

        public RoutingId? RoutingId { get; } = routingId;

        public string? LocalAddr => null;

        public string? RemoteAddr => null;

        public SendFlags LastWriteFlags { get; private set; }

        public List<(byte[] Payload, SendFlags Flags)> Writes { get; } = [];

        public bool Write(
            ZLinkMessage payload,
            SendFlags flags = SendFlags.None)
        {
            LastWriteFlags = flags;
            Writes.Add((payload.Decode<byte[]>(), flags));
            return acceptsWrites;
        }

        public ValueTask CloseAsync()
        {
            return ValueTask.CompletedTask;
        }
    }
}
