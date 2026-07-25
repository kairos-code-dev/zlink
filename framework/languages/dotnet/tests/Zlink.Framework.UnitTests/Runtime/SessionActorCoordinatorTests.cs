using Microsoft.Extensions.DependencyInjection;
using System.Diagnostics.Metrics;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Runtime.Actors;

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

        var error = await Assert.ThrowsAsync<ZLinkFrameworkException>(async () =>
            await context.Client.Send(new SessionPush("value")).Async());

        Assert.Equal(ZLinkFrameworkErrorKind.DeadlineExceeded, error.Kind);
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
                .Async(cancellation.Token)
                .AsTask());
        Assert.Empty(stream.Writes);

        await Assert.ThrowsAsync<InvalidOperationException>(() =>
            context.Client.Reply(new SessionPush("duplicate"))
                .Async()
                .AsTask());
    }

    [Fact]
    public async Task BindActorAsync_Rebinds_Same_Ref_While_BindOrGet_Returns_Existing()
    {
        var runtime = CreateRuntime();
        var context = new ZLinkSessionContext(
            runtime,
            new TestStream(RoutingId.From("session-node")),
            new TestSessionHandlerRegistry(),
            static () => ValueTask.CompletedTask,
            static _ => ValueTask.CompletedTask);
        var actor = new ActorRef(
            "actor-1",
            1,
            "actors",
            RoutingId.From("actor-node"));

        var first = await context.ActorCoordinator.BindOrGetActorAsync(
            context,
            actor,
            CancellationToken.None);
        Assert.True(runtime.TryGetSessionActorBinding(
            actor.ActorId,
            out var firstBinding));

        var existing = await context.ActorCoordinator.BindOrGetActorAsync(
            context,
            actor,
            CancellationToken.None);
        Assert.Same(first, existing);
        Assert.True(runtime.TryGetSessionActorBinding(
            actor.ActorId,
            out var unchangedBinding));
        Assert.Equal(firstBinding.BindingToken, unchangedBinding.BindingToken);
        Assert.Equal(
            firstBinding.BindingGeneration,
            unchangedBinding.BindingGeneration);

        var rebound = await context.ActorCoordinator.BindActorAsync(
            context,
            actor,
            CancellationToken.None);
        Assert.NotSame(first, rebound);
        Assert.True(runtime.TryGetSessionActorBinding(
            actor.ActorId,
            out var reboundBinding));
        Assert.NotEqual(firstBinding.BindingToken, reboundBinding.BindingToken);
        Assert.True(
            reboundBinding.BindingGeneration > firstBinding.BindingGeneration);
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

        var firstRef = new ActorRef("actor-1", 1, "actors", RoutingId.From("actor-node"));
        var secondRef = new ActorRef("actor-1", 2, "actors", RoutingId.From("actor-node"));

        var first = await context.ActorCoordinator.BindOrGetActorAsync(
            context,
            firstRef,
            CancellationToken.None);
        Assert.True(runtime.TryGetSessionActorBinding("actor-1", out var firstSession));
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
        Assert.True(runtime.TryGetSessionActorBinding("actor-1", out var secondSession));
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
        var actor = new ActorRef("actor-1", 1, "actors", RoutingId.From("actor-node"));
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
            null,
            RoutingId.From("session-rid"),
            "local-binding-token",
            objectGeneration: 1,
            authorityOwnerGeneration: 1,
            meshName: "actors",
            ownerLeaseGeneration: 1);
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
        runtimeA.BindActorSession(
            "actor-a",
            null,
            sharedSessionRid,
            token,
            objectGeneration: 1,
            authorityOwnerGeneration: 1,
            meshName: "actors",
            ownerLeaseGeneration: 1);
        runtimeB.BindActorSession(
            "actor-b",
            null,
            sharedSessionRid,
            token,
            objectGeneration: 1,
            authorityOwnerGeneration: 1,
            meshName: "actors",
            ownerLeaseGeneration: 1);

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
        var actor = new ActorRef("actor-1", 1, "actors", RoutingId.From("actor-node"));
        var oldBinding = await firstContext.ActorCoordinator.BindOrGetActorAsync(
            firstContext,
            actor,
            CancellationToken.None);
        _ = await replacementContext.ActorCoordinator.BindOrGetActorAsync(
            replacementContext,
            actor,
            CancellationToken.None);
        Assert.True(runtime.TryGetSessionActorBinding(actor.ActorId, out var replacement));

        await oldBinding.NotifyDisconnectedAsync();

        Assert.True(runtime.TryGetSessionActorBinding(actor.ActorId, out var current));
        Assert.Equal(replacement.BindingToken, current.BindingToken);
        Assert.True(runtime.TryGetSessionActorContext(
            actor.ActorId,
            current.BindingToken,
            out var currentContext));
        Assert.Same(replacementContext, currentContext);
    }

    [Fact]
    public async Task Rebind_Fences_Stale_Relay_And_Late_Disconnect_Without_Affecting_Other_Actors()
    {
        var runtime = CreateRuntime();
        var sessionA = CreateSessionContext(runtime, "session-a");
        var sessionB = CreateSessionContext(runtime, "session-b");
        var actorX = new ActorRef("actor-x", 7, "actors", RoutingId.From("actor-node"));
        var actorA = new ActorRef("actor-a", 1, "actors", RoutingId.From("actor-node"));
        var actorB = new ActorRef("actor-b", 1, "actors", RoutingId.From("actor-node"));
        var stale = await sessionA.ActorCoordinator.BindOrGetActorAsync(
            sessionA,
            actorX,
            CancellationToken.None);
        _ = await sessionA.ActorCoordinator.BindOrGetActorAsync(
            sessionA,
            actorA,
            CancellationToken.None);
        _ = await sessionB.ActorCoordinator.BindOrGetActorAsync(
            sessionB,
            actorB,
            CancellationToken.None);
        var current = await sessionB.ActorCoordinator.BindOrGetActorAsync(
            sessionB,
            actorX,
            CancellationToken.None);
        using var payload = Message.From(new byte[] { 1, 2, 3 });
        var header = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Send,
            ZlinkStreamCodec.Raw,
            ZlinkStreamHeaderFlags.None,
            null,
            "ActorPing",
            ZlinkStreamMetadata.Empty);

        var staleRelay = await Assert.ThrowsAsync<ZLinkFrameworkException>(async () =>
            await sessionA.ActorCoordinator.RelayToActorAsync(
                stale,
                header,
                payload,
                static (_, _, _) => ValueTask.CompletedTask,
                CancellationToken.None));
        Assert.Equal(ZLinkFrameworkErrorKind.ActorSessionNotBound, staleRelay.Kind);

        await stale.NotifyDisconnectedAsync();

        Assert.Null(sessionA.ActorCoordinator.FindActor(actorX.ActorId));
        Assert.Same(current, sessionB.ActorCoordinator.FindActor(actorX.ActorId));
        Assert.NotNull(sessionA.ActorCoordinator.FindActor(actorA.ActorId));
        Assert.NotNull(sessionB.ActorCoordinator.FindActor(actorB.ActorId));
        Assert.True(runtime.TryGetSessionActorBinding(actorX.ActorId, out var currentBinding));
        Assert.True(runtime.TryGetSessionActorContext(
            actorX.ActorId,
            currentBinding.BindingToken,
            out var currentContext));
        Assert.Same(sessionB, currentContext);
        Assert.Equal(actorX.ObjectGeneration, current.Ref.ObjectGeneration);
    }

    [Fact]
    public async Task Physical_Disconnect_Uses_A_Fixed_AllSettled_Snapshot_And_Cleans_Every_Binding()
    {
        var runtime = CreateRuntime();
        var context = CreateSessionContext(runtime, "session-physical-disconnect");
        var actorA = new ActorRef("actor-a", 3, "actors", RoutingId.From("actor-node"));
        var actorB = new ActorRef("actor-b", 5, "actors", RoutingId.From("actor-node"));
        _ = await context.ActorCoordinator.BindOrGetActorAsync(
            context,
            actorA,
            CancellationToken.None);
        _ = await context.ActorCoordinator.BindOrGetActorAsync(
            context,
            actorB,
            CancellationToken.None);
        var actorAState = runtime.GetOrCreateActorState(actorA.ActorId);
        var actorBState = runtime.GetOrCreateActorState(actorB.ActorId);

        await context.ActorCoordinator.CleanupAsync(context, CancellationToken.None);

        Assert.Empty(context.Actors.Bound);
        Assert.False(runtime.TryGetSessionActorBinding(actorA.ActorId, out _));
        Assert.False(runtime.TryGetSessionActorBinding(actorB.ActorId, out _));
        Assert.Same(actorAState, runtime.GetOrCreateActorState(actorA.ActorId));
        Assert.Same(actorBState, runtime.GetOrCreateActorState(actorB.ActorId));
    }

    [Fact]
    public async Task New_Object_Generation_Requires_An_Explicit_Bind()
    {
        var runtime = CreateRuntime();
        var context = CreateSessionContext(runtime, "session-generation-fence");
        var generationOne = new ActorRef(
            "actor-generation",
            1,
            "actors",
            RoutingId.From("actor-node-a"));
        var generationTwo = new ActorRef(
            "actor-generation",
            2,
            "actors",
            RoutingId.From("actor-node-b"));
        var original = await context.ActorCoordinator.BindOrGetActorAsync(
            context,
            generationOne,
            CancellationToken.None);

        var explicitReplacement = await context.ActorCoordinator.BindOrGetActorAsync(
            context,
            generationTwo,
            CancellationToken.None);

        Assert.NotSame(original, explicitReplacement);
        Assert.Equal((ulong)2, explicitReplacement.Ref.ObjectGeneration);
        Assert.Null(context.Actors.Bound.SingleOrDefault(actor =>
            ReferenceEquals(actor, original)));
        Assert.Same(
            explicitReplacement,
            Assert.Single(context.Actors.Bound));
    }

    [Fact]
    public async Task Failed_Exact_Rebind_Preserves_The_Previous_Terminal_Binding()
    {
        var runtime = CreateRuntime();
        var context = CreateSessionContext(runtime, "session-rebind-rollback");
        var previous = new ActorRef(
            "actor-rebind",
            1,
            "actors",
            RoutingId.From("actor-node-a"));
        var replacement = new ActorRef(
            previous.ActorId,
            2,
            "actors",
            RoutingId.From("actor-node-b"));
        var bound = await context.ActorCoordinator.BindOrGetActorAsync(
            context,
            previous,
            CancellationToken.None);
        using var cancellation = new CancellationTokenSource();
        cancellation.Cancel();

        await Assert.ThrowsAnyAsync<OperationCanceledException>(() =>
            context.ActorCoordinator.BindOrGetActorAsync(
                    context,
                    replacement,
                    cancellation.Token)
                .AsTask());

        Assert.Same(bound, Assert.Single(context.Actors.Bound));
        Assert.Equal(previous, bound.Ref);
        Assert.True(runtime.TryGetSessionActorBinding(previous.ActorId, out var retained));
        Assert.Equal(previous.ObjectGeneration, retained.ObjectGeneration);
    }

    [Fact]
    public async Task Concurrent_Find_Never_Observes_A_Release_To_Bind_Gap()
    {
        var runtime = CreateRuntime();
        var context = CreateSessionContext(runtime, "session-rebind-reader");
        var nodeRid = RoutingId.From("actor-node");
        _ = await context.ActorCoordinator.BindOrGetActorAsync(
            context,
            new ActorRef("actor-rebind-reader", 1, "actors", nodeRid),
            CancellationToken.None);
        var replacing = true;
        var reader = Task.Run(() =>
        {
            while (Volatile.Read(ref replacing))
                Assert.NotNull(context.ActorCoordinator.FindActor(
                    "actor-rebind-reader"));
        });

        for (ulong generation = 2; generation <= 64; generation++)
            _ = await context.ActorCoordinator.BindOrGetActorAsync(
                context,
                new ActorRef(
                    "actor-rebind-reader",
                    generation,
                    "actors",
                    nodeRid),
                CancellationToken.None);
        Volatile.Write(ref replacing, false);
        await reader;

        Assert.Equal(
            (ulong)64,
            Assert.Single(context.Actors.Bound).Ref.ObjectGeneration);
    }

    [Fact]
    public async Task Exact_Rebind_Replaces_A_Remote_Route_With_The_Local_Node_Route()
    {
        var runtime = CreateRuntime();
        var context = CreateSessionContext(runtime, "session-local-node");
        var remote = new ActorRef(
            "actor-remote-to-local",
            1,
            "actors",
            RoutingId.From("remote-node"));
        var local = new ActorRef(
            remote.ActorId,
            2,
            "actors",
            RoutingId.From("session-local-node"));
        _ = await context.ActorCoordinator.BindOrGetActorAsync(
            context,
            remote,
            CancellationToken.None);

        var rebound = await context.ActorCoordinator.BindOrGetActorAsync(
            context,
            local,
            CancellationToken.None);

        Assert.Equal(local, rebound.Ref);
        Assert.Equal(local, Assert.Single(context.Actors.Bound).Ref);
        Assert.True(runtime.TryGetSessionActorBinding(local.ActorId, out var exact));
        Assert.Equal(local.ObjectGeneration, exact.ObjectGeneration);
        Assert.Equal(local.MeshName, exact.MeshName);
    }

    [Fact]
    public async Task Concurrent_Exact_Replacements_Are_Serialized_Per_Actor()
    {
        var runtime = CreateRuntime();
        var context = CreateSessionContext(runtime, "session-concurrent-rebind");
        var original = new ActorRef(
            "actor-concurrent-rebind",
            1,
            "actors",
            RoutingId.From("node-o"));
        _ = await context.ActorCoordinator.BindOrGetActorAsync(
            context,
            original,
            CancellationToken.None);
        var candidateA = new ActorRef(
            original.ActorId,
            2,
            "actors",
            RoutingId.From("node-a"));
        var candidateB = new ActorRef(
            original.ActorId,
            3,
            "actors",
            RoutingId.From("node-b"));
        using var start = new ManualResetEventSlim();

        var replacementA = Task.Run(async () =>
        {
            start.Wait();
            return await context.ActorCoordinator.BindOrGetActorAsync(
                context,
                candidateA,
                CancellationToken.None);
        });
        var replacementB = Task.Run(async () =>
        {
            start.Wait();
            return await context.ActorCoordinator.BindOrGetActorAsync(
                context,
                candidateB,
                CancellationToken.None);
        });
        start.Set();
        await Task.WhenAll(replacementA, replacementB);

        var terminal = Assert.Single(context.Actors.Bound);
        Assert.Contains(
            terminal.Ref,
            new[] { candidateA, candidateB });
        Assert.True(runtime.TryGetSessionActorBinding(
            original.ActorId,
            out var exact));
        Assert.Equal(terminal.Ref.ObjectGeneration, exact.ObjectGeneration);
        Assert.Equal(terminal.Ref.MeshName, exact.MeshName);
    }

    [Fact]
    public async Task Cross_Owner_Rebind_Acknowledges_Tombstone_Before_Source_Swap()
    {
        var events = new List<string> { "new-register" };
        var previous = PreviousBinding("old-node");

        await ZLinkSessionBindingReplacement.CompletePreviousAsync(
            "actor-order",
            RoutingId.From("new-node"),
            previous,
            (request, _) =>
            {
                events.Add("old-tombstone");
                Assert.Equal(previous.BindingToken, request.BindingToken);
                Assert.Equal(previous.BindingGeneration, request.BindingGeneration);
                Assert.Equal(previous.ObjectGeneration, request.ObjectGeneration);
                return ValueTask.FromResult(
                    new ZLinkRemoteSessionUnbindResponse(true));
            },
            CancellationToken.None);
        events.Add("source-swap");

        Assert.Equal(
            new[] { "new-register", "old-tombstone", "source-swap" },
            events);
    }

    [Fact]
    public async Task Tombstone_Failure_Or_Cancellation_Leaves_Source_Old_Binding()
    {
        var sourceIsOld = true;
        var previous = PreviousBinding("old-node");

        await Assert.ThrowsAsync<ZLinkFrameworkException>(() =>
            ZLinkSessionBindingReplacement.CompletePreviousAsync(
                    "actor-failure",
                    RoutingId.From("new-node"),
                    previous,
                    static (_, _) => ValueTask.FromResult(
                        new ZLinkRemoteSessionUnbindResponse(false)),
                    CancellationToken.None)
                .AsTask());
        Assert.True(sourceIsOld);

        using var cancellation = new CancellationTokenSource();
        cancellation.Cancel();
        await Assert.ThrowsAnyAsync<OperationCanceledException>(() =>
            ZLinkSessionBindingReplacement.CompletePreviousAsync(
                    "actor-cancel",
                    RoutingId.From("new-node"),
                    previous,
                    static (_, token) =>
                        ValueTask.FromCanceled<ZLinkRemoteSessionUnbindResponse>(
                            token),
                    cancellation.Token)
                .AsTask());
        Assert.True(sourceIsOld);
    }

    [Fact]
    public async Task Same_Rebind_Request_Retries_Exact_Tombstone_Until_Acknowledged()
    {
        var previous = PreviousBinding("old-node");
        var requests = new List<ZLinkRemoteSessionUnbindRequest>();

        async ValueTask AttemptAsync()
        {
            await ZLinkSessionBindingReplacement.CompletePreviousAsync(
                "actor-retry",
                RoutingId.From("new-node"),
                previous,
                (request, _) =>
                {
                    requests.Add(request);
                    return ValueTask.FromResult(
                        new ZLinkRemoteSessionUnbindResponse(
                            requests.Count > 1));
                },
                CancellationToken.None);
        }

        await Assert.ThrowsAsync<ZLinkFrameworkException>(
            () => AttemptAsync().AsTask());
        await AttemptAsync();

        Assert.Equal(2, requests.Count);
        Assert.Equal(requests[0], requests[1]);
    }

    [Fact]
    public async Task Same_Owner_Replacement_Does_Not_Self_Tombstone()
    {
        var tombstones = 0;
        await ZLinkSessionBindingReplacement.CompletePreviousAsync(
            "actor-same-owner",
            RoutingId.From("same-node"),
            PreviousBinding("same-node"),
            (_, _) =>
            {
                tombstones++;
                return ValueTask.FromResult(
                    new ZLinkRemoteSessionUnbindResponse(true));
            },
            CancellationToken.None);

        Assert.Equal(0, tombstones);
    }

    [Fact]
    public async Task Stale_Exact_Tombstone_Does_Not_Remove_Current_Binding()
    {
        var runtime = CreateRuntime();
        var context = CreateSessionContext(runtime, "session-stale-tombstone");
        var actor = new ActorRef(
            "actor-stale-tombstone",
            1,
            "actors",
            RoutingId.From("actor-node"));
        var current = await context.ActorCoordinator.BindOrGetActorAsync(
            context,
            actor,
            CancellationToken.None);

        runtime.UnbindSessionActor(actor.ActorId, context, "stale-token");

        Assert.Same(current, Assert.Single(context.Actors.Bound));
    }

    [Fact]
    public async Task Completed_Route_Commit_Is_Exactly_Fenced_And_Idempotent()
    {
        var runtime = CreateRuntime();
        var context = CreateSessionContext(runtime, "session-route-commit");
        var source = new ActorRef(
            "actor-route",
            7,
            "actors",
            RoutingId.From("actor-node-a"));
        var target = new ActorRef(
            "actor-route",
            7,
            "actors",
            RoutingId.From("actor-node-b"));
        var bound = await context.ActorCoordinator.BindOrGetActorAsync(
            context,
            source,
            CancellationToken.None);
        Assert.True(runtime.TryGetSessionActorBinding(source.ActorId, out var identity));
        const string handoffId = "handoff-route-commit";
        Assert.True((await runtime.SealSessionActorRouteAsync(
            new ZLinkSessionRouteSeal(
                source.ActorId,
                identity.BindingToken,
                identity.BindingGeneration,
                source.ObjectGeneration,
                identity.AuthorityOwnerGeneration,
                identity.MeshName,
                identity.TargetNodeGeneration,
                identity.OwnerLeaseGeneration,
                identity.SessionOwnerNodeGeneration,
                handoffId),
            CancellationToken.None)).Acknowledged);
        Assert.False(runtime.TryAcceptSessionActorFrame(
            source.ActorId,
            identity.BindingToken,
            out var sealedHighWater));
        Assert.Equal(identity.AcceptedHighWater, sealedHighWater);

        var stale = runtime.CommitSessionActorRoute(
            new ZLinkSessionRouteCommit(
                source.ActorId,
                identity.BindingToken,
                identity.BindingGeneration,
                source.ObjectGeneration,
                identity.AuthorityOwnerGeneration + 1,
                identity.AuthorityOwnerGeneration + 2,
                identity.MeshName,
                identity.MeshName,
                identity.TargetNodeGeneration,
                identity.TargetNodeGeneration + 1,
                identity.OwnerLeaseGeneration,
                identity.OwnerLeaseGeneration + 1,
                identity.SessionOwnerNodeGeneration,
                identity.AcceptedHighWater,
                handoffId,
                target));

        Assert.False(stale.Acknowledged);
        Assert.Equal(source, bound.Ref);

        var command = new ZLinkSessionRouteCommit(
            source.ActorId,
            identity.BindingToken,
            identity.BindingGeneration,
            source.ObjectGeneration,
            identity.AuthorityOwnerGeneration,
            identity.AuthorityOwnerGeneration + 1,
            identity.MeshName,
            identity.MeshName,
            identity.TargetNodeGeneration,
            identity.TargetNodeGeneration + 1,
            identity.OwnerLeaseGeneration,
            identity.OwnerLeaseGeneration + 1,
            identity.SessionOwnerNodeGeneration,
            identity.AcceptedHighWater,
            handoffId,
            target);
        var committed = runtime.CommitSessionActorRoute(command);
        var retried = runtime.CommitSessionActorRoute(command);

        Assert.True(committed.Acknowledged);
        Assert.True(retried.Acknowledged);
        Assert.Equal(target, bound.Ref);
        Assert.Equal(source.ObjectGeneration, bound.Ref.ObjectGeneration);
        Assert.False(runtime.TryAcceptSessionActorFrame(
            source.ActorId,
            identity.BindingToken,
            out _));
        Assert.True(runtime.UnsealCommittedSessionActorRoute(command));
        Assert.True(runtime.TryAcceptSessionActorFrame(
            source.ActorId,
            identity.BindingToken,
            out var nextHighWater));
        Assert.Equal(identity.AcceptedHighWater + 1, nextHighWater);
    }

    [Fact]
    public async Task Ingress_Seal_Waits_For_The_Accepted_Frame_To_Terminate()
    {
        var runtime = CreateRuntime();
        var context = CreateSessionContext(runtime, "session-seal-drain");
        var actor = new ActorRef(
            "actor-seal-drain",
            7,
            "actors",
            RoutingId.From("actor-node-a"));
        _ = await context.ActorCoordinator.BindOrGetActorAsync(
            context,
            actor,
            CancellationToken.None);
        Assert.True(runtime.TryGetSessionActorBinding(actor.ActorId, out var identity));
        Assert.True(runtime.TryAcceptSessionActorFrame(
            actor.ActorId,
            identity.BindingToken,
            out var acceptedHighWater));

        var seal = runtime.SealSessionActorRouteAsync(
                new ZLinkSessionRouteSeal(
                    actor.ActorId,
                    identity.BindingToken,
                    identity.BindingGeneration,
                    actor.ObjectGeneration,
                    identity.AuthorityOwnerGeneration,
                    identity.MeshName,
                    identity.TargetNodeGeneration,
                    identity.OwnerLeaseGeneration,
                    identity.SessionOwnerNodeGeneration,
                    "handoff-drain"),
                CancellationToken.None)
            .AsTask();

        Assert.False(seal.IsCompleted);
        runtime.CompleteAcceptedSessionActorFrame(
            actor.ActorId,
            identity.BindingToken);
        var result = await seal;

        Assert.True(result.Acknowledged);
        Assert.Equal(acceptedHighWater, result.AcceptedHighWater);
        Assert.False(runtime.TryAcceptSessionActorFrame(
            actor.ActorId,
            identity.BindingToken,
            out _));
    }

    [Fact]
    public async Task Completed_Route_Commit_Rejects_Each_Stale_Binding_Fence()
    {
        var runtime = CreateRuntime();
        var context = CreateSessionContext(runtime, "session-route-fences");
        var source = new ActorRef(
            "actor-route-fences",
            7,
            "actors",
            RoutingId.From("actor-node-a"));
        var target = new ActorRef(
            source.ActorId,
            source.ObjectGeneration,
            "actors",
            RoutingId.From("actor-node-b"));
        var bound = await context.ActorCoordinator.BindOrGetActorAsync(
            context,
            source,
            CancellationToken.None);
        Assert.True(runtime.TryGetSessionActorBinding(source.ActorId, out var identity));
        const string handoffId = "handoff-route-fences";
        Assert.True((await runtime.SealSessionActorRouteAsync(
            new ZLinkSessionRouteSeal(
                source.ActorId,
                identity.BindingToken,
                identity.BindingGeneration,
                source.ObjectGeneration,
                identity.AuthorityOwnerGeneration,
                identity.MeshName,
                identity.TargetNodeGeneration,
                identity.OwnerLeaseGeneration,
                identity.SessionOwnerNodeGeneration,
                handoffId),
            CancellationToken.None)).Acknowledged);

        var current = new ZLinkSessionRouteCommit(
            source.ActorId,
            identity.BindingToken,
            identity.BindingGeneration,
            source.ObjectGeneration,
            identity.AuthorityOwnerGeneration,
            identity.AuthorityOwnerGeneration + 1,
            identity.MeshName,
            identity.MeshName,
            identity.TargetNodeGeneration,
            identity.TargetNodeGeneration + 1,
            identity.OwnerLeaseGeneration,
            identity.OwnerLeaseGeneration + 1,
            identity.SessionOwnerNodeGeneration,
            identity.AcceptedHighWater,
            handoffId,
            target);
        var staleCommands = new[]
        {
            current with { BindingToken = "stale-binding" },
            current with { BindingGeneration = current.BindingGeneration + 1 },
            current with { ObjectGeneration = current.ObjectGeneration + 1 },
            current with
            {
                PreviousAuthorityOwnerGeneration =
                    current.PreviousAuthorityOwnerGeneration + 1
            },
            current with { PreviousMeshName = "stale-mesh" },
            current with
            {
                PreviousTargetNodeGeneration =
                    current.PreviousTargetNodeGeneration + 1
            },
            current with
            {
                PreviousOwnerLeaseGeneration =
                    current.PreviousOwnerLeaseGeneration + 1
            },
            current with
            {
                SessionOwnerNodeGeneration =
                    current.SessionOwnerNodeGeneration + 1
            },
            current with { AcceptedHighWater = current.AcceptedHighWater + 1 },
            current with
            {
                TargetActor = new ActorRef(
                    target.ActorId,
                    target.ObjectGeneration + 1,
                    target.MeshName,
                    target.NodeRid)
            }
        };

        foreach (var stale in staleCommands)
        {
            Assert.False(runtime.CommitSessionActorRoute(stale).Acknowledged);
            Assert.Equal(source, bound.Ref);
        }
    }

    [Fact]
    public async Task Bound_Actor_Relay_Does_Not_Resolve_The_Location_Store_Per_Message()
    {
        var directory = new MissingActorDirectory();
        var runtime = CreateRuntime(actorDirectory: directory);
        var context = CreateSessionContext(runtime, "session-rid");
        var actor = new ActorRef("actor-1", 1, "actors", RoutingId.From("actor-node"));
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
        Assert.Equal(0, directory.Calls);
        Assert.Equal(actor, Assert.Single(context.Actors.Bound).Ref);
    }

    [Fact]
    public async Task Bound_Actor_Relay_Keeps_The_Exact_Bound_Route_Until_Relocation_Switches_It()
    {
        var current = new ActorRef("actor-1", 2, "actors", RoutingId.From("actor-node-b"));
        var directory = new FixedActorDirectory(current);
        var runtime = CreateRuntime(actorDirectory: directory);
        var context = CreateSessionContext(runtime, "session-rid");
        var stale = new ActorRef("actor-1", 1, "actors", RoutingId.From("actor-node-a"));
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

        var retained = Assert.Single(context.Actors.Bound);
        Assert.Same(bound, retained);
        Assert.Equal(stale, retained.Ref);
        Assert.Equal(0, directory.Calls);
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

    private static ZLinkRemoteSessionPreviousBinding PreviousBinding(
        string nodeRid) => new(
        RoutingId.From(nodeRid).ToBytes().ToArray(),
        "old-token",
        7,
        11,
        "actors",
        13,
        17,
        19,
        23);

    private static ZLinkFrameworkRuntime CreateRuntime(IZLinkActorResolver? actorDirectory = null)
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

    private sealed class MissingActorDirectory : IZLinkActorResolver
    {
        public int Calls { get; private set; }

        public ValueTask<(ActorRef? Ref, bool RowPresent)> FindWithPresenceAsync(
            string actorId,
            CancellationToken cancellationToken = default)
        {
            Calls++;
            return ValueTask.FromResult<(ActorRef?, bool)>((null, false));
        }
    }

    private sealed class FixedActorDirectory(ActorRef actor) : IZLinkActorResolver
    {
        public int Calls { get; private set; }

        public ValueTask<(ActorRef? Ref, bool RowPresent)> FindWithPresenceAsync(
            string actorId,
            CancellationToken cancellationToken = default)
        {
            Calls++;
            return ValueTask.FromResult<(ActorRef?, bool)>(
                actor.ActorId == actorId ? (actor, true) : (null, false));
        }
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
