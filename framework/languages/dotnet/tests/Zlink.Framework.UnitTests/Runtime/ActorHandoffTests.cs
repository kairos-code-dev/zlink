using Systems.Zlink.Stream.Connector.Contracts;
using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.UnitTests.Runtime;

public sealed class ActorHandoffTests
{
    [Fact]
    public async Task Unregistered_Transfer_Adapter_Uses_The_Frozen_Empty_State()
    {
        var state = await ZLinkActorRemoteJoiner.CaptureTransferStateAsync(
            new ServiceCollection().BuildServiceProvider(),
            transfer: null,
            new ActorWithoutTransferAdapter(),
            CancellationToken.None);

        Assert.Same(ZLinkMessage.Empty, state);
    }

    [Fact]
    public void InFlightHandoffOrder_PreservesArrivalOrder()
    {
        var state = new ZLinkActorRuntimeState("actor-1");
        state.Handoff.BeginCapture();

        Capture(state, "P1", "session-1");
        Capture(state, "P2", "session-1");
        Capture(state, "P3", "session-1");

        var frames = state.Handoff.SnapshotFrames();
        Assert.Equal(["P1", "P2", "P3"], frames.Select(DecodeBody));
    }

    [Fact]
    public void DirectOvertakesPrevented_ImportedBacklogStaysAheadOfTargetArrivals()
    {
        var source = new ZLinkActorRuntimeState("actor-1");
        source.Handoff.BeginCapture();
        Capture(source, "B1", "session-1");
        Capture(source, "B2", "session-1");

        var target = new ZLinkActorRuntimeState("actor-1");
        target.Handoff.Import(CommitRequest("handoff-1", source.Handoff.SnapshotFrames()), out _);
        Capture(target, "D1", "session-2");

        var frames = target.Handoff.PrepareImportedReplay([]);
        Assert.Equal(["B1", "B2", "D1"], frames.Select(DecodeBody));
    }

    [Fact]
    public void BoundSessionCrossMoveOrder_PreservesSessionRouteAndSequence()
    {
        var source = new ZLinkActorRuntimeState("actor-1");
        source.Handoff.BeginCapture();
        Capture(source, "S1", "bound-session");
        Capture(source, "S2", "bound-session");

        var target = new ZLinkActorRuntimeState("actor-1");
        target.Handoff.Import(CommitRequest("handoff-1", source.Handoff.SnapshotFrames()), out _);
        Capture(target, "S3", "bound-session");
        Capture(target, "S4", "bound-session");

        var frames = target.Handoff.PrepareImportedReplay([]);
        Assert.Equal(["S1", "S2", "S3", "S4"], frames.Select(DecodeBody));
        Assert.All(frames, frame => Assert.Equal("bound-session", RoutingId.From(frame.SourceSessionRid).ToString()));
    }

    [Fact]
    public void StragglerForwardThenFailFast_UsesBoundedWindow()
    {
        var time = new ManualTimeProvider();
        var state = new ZLinkActorRuntimeState("actor-1", time);
        var source = ActorRef("node-a", 1);
        var target = ActorRef("node-b", 2);
        state.BindNativeActorRef(target);
        state.Handoff.BeginCapture();
        _ = state.Handoff.CutoverCaptureToForwarding(0, source, target);
        state.Handoff.CommitForwardingCutover(TimeSpan.FromMilliseconds(50));

        Assert.Equal(
            ZLinkActorFrameRoute.Forward,
            state.Handoff.ResolveFrameRoute(state.NativeActorRef, source, out var forwarded));
        Assert.Equal(target, forwarded);

        time.Advance(TimeSpan.FromMilliseconds(150));
        Assert.Equal(
            ZLinkActorFrameRoute.Stale,
            state.Handoff.ResolveFrameRoute(state.NativeActorRef, source, out _));
    }

    [Fact]
    public void ForwardingMappingEviction_ReplacesTheNodeActorEntry()
    {
        var state = new ZLinkActorRuntimeState("actor-1");
        var firstSource = ActorRef("node-a", 1);
        var firstTarget = ActorRef("node-b", 2);
        var nextSource = ActorRef("node-a", 3);
        var nextTarget = ActorRef("node-c", 4);
        state.BindNativeActorRef(nextTarget);

        state.Handoff.BeginCapture();
        _ = state.Handoff.CutoverCaptureToForwarding(0, firstSource, firstTarget);
        state.Handoff.CommitForwardingCutover(TimeSpan.FromSeconds(1));
        state.Handoff.CompleteSourceMigration();
        state.Handoff.BeginCapture();
        _ = state.Handoff.CutoverCaptureToForwarding(0, nextSource, nextTarget);
        state.Handoff.CommitForwardingCutover(TimeSpan.FromSeconds(1));

        Assert.Equal(
            ZLinkActorFrameRoute.Stale,
            state.Handoff.ResolveFrameRoute(state.NativeActorRef, firstSource, out _));
        Assert.Equal(
            ZLinkActorFrameRoute.Forward,
            state.Handoff.ResolveFrameRoute(state.NativeActorRef, nextSource, out var forwarded));
        Assert.Equal(nextTarget, forwarded);
    }

    [Fact]
    public void MigratedAndDestroyedTransitions_PreserveThenReleaseBoundSessionIdentity()
    {
        var state = new ZLinkActorRuntimeState("actor-1");
        var source = ActorRef("node-a", 1);
        var target = ActorRef("node-b", 2);
        var sessionRid = RoutingId.From("session-1");
        state.BindNativeActorRef(target);
        state.BindSession(RoutingId.From("session-node"), sessionRid, "binding-1");
        state.Handoff.BeginCapture();
        _ = state.Handoff.CutoverCaptureToForwarding(0, source, target);
        state.Handoff.CommitForwardingCutover(TimeSpan.FromSeconds(1));

        state.RetireMigratedActorInstance(source);

        Assert.Equal(target, state.NativeActorRef);
        Assert.Equal(source, state.RetiredLocalActorRef);
        Assert.True(state.TryGetBoundSession(out var retained));
        Assert.Equal(sessionRid, retained.SessionRid);
        Assert.Equal("binding-1", retained.BindingToken);

        var released = state.ClearAfterDestroy();

        Assert.Equal(retained, released);
        Assert.Null(state.NativeActorRef);
        Assert.Null(state.RetiredLocalActorRef);
        Assert.False(state.TryGetBoundSession(out _));
    }

    [Fact]
    public void HandoffCompletion_RequiresMatchingTransfer()
    {
        var state = new ZLinkActorRuntimeState("actor-1");
        state.Handoff.Import(CommitRequest("handoff-1", []), out _);

        Assert.Throws<InvalidOperationException>(() => state.Handoff.Complete("handoff-other"));
        Assert.Throws<InvalidOperationException>(() => state.Handoff.Complete("handoff-1"));
        Assert.Empty(state.Handoff.PrepareImportedReplay([]));
        Assert.Empty(state.Handoff.SnapshotFinalReplay());
        state.Handoff.Complete("handoff-1");
    }

    [Fact]
    public async Task LocalDispatch_IsRejectedWhileHandoffOwnsTheActor()
    {
        var state = new ZLinkActorRuntimeState("actor-1");
        var invoked = false;
        state.Handoff.BeginCapture();

        var exception = await Assert.ThrowsAsync<ZLinkFrameworkException>(async () =>
            await state.ExecuteDispatchAsync(
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Send,
                    ZlinkStreamCodec.Raw,
                    ZlinkStreamHeaderFlags.None,
                    null,
                    "blocked",
                    ZlinkStreamMetadata.Empty),
                _ =>
                {
                    invoked = true;
                    return ValueTask.CompletedTask;
                },
                CancellationToken.None));

        Assert.Equal(ZLinkFrameworkErrorKind.ActorRouteNotFound, exception.Kind);
        Assert.False(invoked);
        _ = state.Handoff.AbortCapture();
    }

    [Fact]
    public async Task DispatchOwnership_DoesNotEscapeIntoAChildTask()
    {
        var state = new ZLinkActorRuntimeState("actor-1");
        var childStarted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var releaseChild = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        Task? escapedChild = null;

        await state.ExecuteDispatchAsync(
            Header("first"),
            _ =>
            {
                escapedChild = Task.Run(async () =>
                {
                    childStarted.TrySetResult();
                    await releaseChild.Task;
                    await state.ExecuteHandoffTransitionAsync(() => true, CancellationToken.None);
                });
                return ValueTask.CompletedTask;
            },
            CancellationToken.None);
        await childStarted.Task;

        var secondEntered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var releaseSecond = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var secondDispatch = state.ExecuteDispatchAsync(
                Header("second"),
                async _ =>
                {
                    secondEntered.TrySetResult();
                    await releaseSecond.Task;
                },
                CancellationToken.None)
            .AsTask();
        await secondEntered.Task;

        releaseChild.TrySetResult();
        await Task.Delay(25);
        Assert.False(escapedChild!.IsCompleted);

        releaseSecond.TrySetResult();
        await secondDispatch;
        await escapedChild;
    }

    [Fact]
    public void SourceCommitSnapshot_RetainsFramesForRollback_AndReturnsOnlyTrailingFramesOnSuccess()
    {
        var state = new ZLinkActorRuntimeState("actor-1");
        state.Handoff.BeginCapture();
        Capture(state, "B1", "session-1");
        Capture(state, "B2", "session-1");

        var committed = state.Handoff.SnapshotFrames();
        Capture(state, "T1", "session-1");

        Assert.Equal(["B1", "B2"], committed.Select(DecodeBody));
        var trailing = state.Handoff.CutoverCaptureToForwarding(
            committed.Count,
            ActorRef("node-a", 1),
            ActorRef("node-b", 2));
        Assert.Equal(["T1"], trailing.Select(DecodeBody));
        _ = state.Handoff.AbortCapture();
    }

    [Fact]
    public void AbortedCapture_ReturnsEveryFrameForLocalReplay()
    {
        var state = new ZLinkActorRuntimeState("actor-1");
        state.Handoff.BeginCapture();
        Capture(state, "B1", "session-1");
        Capture(state, "B2", "session-1");

        Assert.Equal(["B1", "B2"], state.Handoff.AbortCapture().Select(DecodeBody));
    }

    [Fact]
    public void PausedCapture_RetainsEveryFrameUntilRemoteCompletionCommits()
    {
        var state = new ZLinkActorRuntimeState("actor-1");
        var sourceActor = ActorRef("node-a", 1);
        state.BindNativeActorRef(sourceActor);
        state.Handoff.BeginCapture();
        Capture(state, "B1", "session-1");
        var committed = state.Handoff.SnapshotFrames();
        Capture(state, "T1", "session-1");

        Assert.Equal(
            ["T1"],
            state.Handoff.CutoverCaptureToForwarding(
                    committed.Count,
                    sourceActor,
                    ActorRef("node-b", 2))
                .Select(DecodeBody));
        Assert.Equal(["B1", "T1"], state.Handoff.AbortCapture().Select(DecodeBody));
        Assert.Equal(
            ZLinkActorFrameRoute.Current,
            state.Handoff.ResolveFrameRoute(sourceActor, sourceActor, out _));
    }

    [Fact]
    public void ImportedCapture_PlacesSourceTrailingFramesBeforeTargetArrivals()
    {
        var source = new ZLinkActorRuntimeState("actor-1");
        source.Handoff.BeginCapture();
        Capture(source, "B1", "session-1");
        var initial = source.Handoff.SnapshotFrames();
        Capture(source, "T1", "session-1");
        var trailing = source.Handoff.CutoverCaptureToForwarding(
            initial.Count,
            ActorRef("node-a", 1),
            ActorRef("node-b", 2));

        var target = new ZLinkActorRuntimeState("actor-1");
        var commit = CommitRequest("handoff-1", initial);
        Assert.True(target.Handoff.Import(commit, out _));
        Capture(target, "D1", "session-2");

        var replay = target.Handoff.PrepareImportedReplay(trailing);
        Assert.Equal(["B1", "T1", "D1"], replay.Select(DecodeBody));
        foreach (var _ in replay) target.Handoff.AcknowledgeReplayedFrame();
        Assert.Empty(target.Handoff.PrepareImportedReplay(trailing));
        Assert.False(target.Handoff.Import(commit, out _));
    }

    [Fact]
    public void ReplayAcknowledgement_RetainsTheFailedSuffixForRetry()
    {
        var state = new ZLinkActorRuntimeState("actor-1");
        state.Handoff.Import(CommitRequest("handoff-1", []), out _);
        Capture(state, "P1", "session-1");
        Capture(state, "P2", "session-1");
        Capture(state, "P3", "session-1");

        Assert.Equal(
            ["P1", "P2", "P3"],
            state.Handoff.PrepareImportedReplay([]).Select(DecodeBody));
        state.Handoff.AcknowledgeReplayedFrame();

        Assert.Equal(
            ["P2", "P3"],
            state.Handoff.PrepareImportedReplay([]).Select(DecodeBody));
    }

    [Fact]
    public void SourceCutover_AtomicallyStopsCaptureAndStartsForwarding()
    {
        var state = new ZLinkActorRuntimeState("actor-1");
        var source = ActorRef("node-a", 1);
        var target = ActorRef("node-b", 2);
        state.BindNativeActorRef(source);
        state.Handoff.BeginCapture();
        Capture(state, "P1", "session-1");

        var trailing = state.Handoff.CutoverCaptureToForwarding(0, source, target);

        Assert.Equal(["P1"], trailing.Select(DecodeBody));
        using var body = Message.From(System.Text.Encoding.UTF8.GetBytes("after-cutover"));
        using var frame = Frame(body, source, "session-1");
        Assert.False(state.Handoff.TryCapture(frame));
        Assert.Equal(
            ZLinkActorFrameRoute.Forward,
            state.Handoff.ResolveFrameRoute(state.NativeActorRef, source, out var forwarded));
        Assert.Equal(target, forwarded);
    }

    [Fact]
    public async Task SourceCaptureAndCutover_RacePlacesFrameInExactlyOnePath()
    {
        for (var iteration = 0; iteration < 100; iteration++)
        {
            var state = new ZLinkActorRuntimeState("actor-1");
            var source = ActorRef("node-a", 1);
            var target = ActorRef("node-b", 2);
            state.BindNativeActorRef(source);
            state.Handoff.BeginCapture();
            using var body = Message.From(System.Text.Encoding.UTF8.GetBytes("race"));
            using var frame = Frame(body, source, "session-race");
            using var barrier = new Barrier(2);

            var capture = Task.Run(() =>
            {
                barrier.SignalAndWait();
                return state.Handoff.TryCapture(frame);
            });
            var cutover = Task.Run(() =>
            {
                barrier.SignalAndWait();
                return state.Handoff.CutoverCaptureToForwarding(0, source, target);
            });

            var captured = await capture;
            var trailing = await cutover;
            Assert.Equal(
                ZLinkActorFrameRoute.Forward,
                state.Handoff.ResolveFrameRoute(source, source, out _));
            Assert.Equal(captured ? 1 : 0, trailing.Count);
            _ = state.Handoff.AbortCapture();
        }
    }

    [Fact]
    public async Task TargetFinalCaptureAndCompletion_RaceCannotSkipAFrame()
    {
        for (var iteration = 0; iteration < 100; iteration++)
        {
            var state = new ZLinkActorRuntimeState("actor-1");
            var handoffId = $"handoff-{iteration}";
            state.Handoff.Import(CommitRequest(handoffId, []), out _);
            Assert.Empty(state.Handoff.PrepareImportedReplay([]));
            var source = ActorRef("node-a", 1);
            using var body = Message.From(System.Text.Encoding.UTF8.GetBytes("race"));
            using var frame = Frame(body, source, "session-race");
            using var barrier = new Barrier(2);

            var capture = Task.Run(() =>
            {
                barrier.SignalAndWait();
                return state.Handoff.TryCapture(frame);
            });
            var completion = Task.Run(() =>
            {
                barrier.SignalAndWait();
                try
                {
                    state.Handoff.Complete(handoffId);
                    return true;
                }
                catch (InvalidOperationException)
                {
                    return false;
                }
            });

            var captured = await capture;
            var completed = await completion;
            Assert.True(captured ^ completed);
            if (captured)
            {
                state.Handoff.AcknowledgeReplayedFrame();
                state.Handoff.Complete(handoffId);
            }
        }
    }

    [Fact]
    public void RequestHandoff_PreservesReplyRoutingFields()
    {
        var state = new ZLinkActorRuntimeState("actor-1");
        state.Handoff.BeginCapture();
        Capture(
            state,
            "request",
            "request-session",
            ZlinkStreamMessageKind.Request,
            requestId: 42,
            flags: 7);

        var frames = state.Handoff.SnapshotFrames();
        _ = state.Handoff.AbortCapture();
        var restored = ZLinkActorHandoffFrames.Restore(ActorRef("node-b", 2), frames);
        try
        {
            Assert.Equal(1, restored.Count);
            var restoredFrame = restored[0];
            Assert.Equal((ulong)42, restoredFrame.RequestId);
            Assert.Equal((uint)7, restoredFrame.Flags);
            Assert.Equal("node-a", restoredFrame.ReplyActor.NodeRid.ToString());
            Assert.Equal((ulong)1, restoredFrame.ReplyActor.Generation);
            Assert.Equal("request-session", restoredFrame.SourceSessionRid.ToString());
            Assert.Equal(ZlinkStreamMessageKind.Request, restoredFrame.Header.Kind);
        }
        finally
        {
            restored.Dispose();
        }
    }

    [Fact]
    public async Task DuplicateImport_WaitsForTheOriginalPreparationResult()
    {
        var state = new ZLinkActorRuntimeState("actor-1");

        var commit = CommitRequest("handoff-1", []);
        Assert.True(state.Handoff.Import(commit, out var originalPreparation));
        Assert.False(state.Handoff.Import(commit, out var duplicatePreparation));
        Assert.Same(originalPreparation, duplicatePreparation);
        Assert.False(duplicatePreparation.IsCompleted);

        var reply = ZLinkRemoteActorJoinPackets.CreateJoinReply(true, ActorRef("node-b", 2));
        state.Handoff.AcceptPreparation("handoff-1", reply);

        Assert.Same(reply, await duplicatePreparation);
    }

    [Fact]
    public void BeginCapture_RejectsConcurrentTransactions_AndRetiresCompletedIdentity()
    {
        var state = new ZLinkActorRuntimeState("actor-1");
        state.Handoff.BeginCapture();
        Assert.Throws<InvalidOperationException>(state.Handoff.BeginCapture);
        _ = state.Handoff.AbortCapture();

        Assert.True(state.Handoff.Import(CommitRequest("handoff-arrive", []), out _));
        Assert.Empty(state.Handoff.PrepareImportedReplay([]));
        Assert.Empty(state.Handoff.SnapshotFinalReplay());
        state.Handoff.Complete("handoff-arrive");

        state.Handoff.BeginCapture();
        _ = state.Handoff.AbortCapture();
        Assert.True(state.Handoff.Import(CommitRequest("handoff-return", []), out _));
    }

    [Fact]
    public void PendingAdmission_IsCorrelatedByHandoffId_AndEnforcesItsDeadline()
    {
        var time = new ManualTimeProvider();
        var admissions = new ZLinkActorHandoffAdmissions(time);
        var request = new ZLinkRemoteActorAdmissionRequest(
            "actor-1",
            "warrior",
            [],
            [],
            "application/json",
            [],
            "handoff-1",
            (time.GetUtcNow() + TimeSpan.FromSeconds(5)).ToUnixTimeMilliseconds());
        var reply = new ZLinkRemoteActorAdmissionReply(true, "application/json", [], request.DeadlineUnixTimeMilliseconds);
        var targetSpotRid = RoutingId.From("spot-1");

        admissions.Register(request, targetSpotRid, reply);
        Assert.True(admissions.TryGetReply(request, targetSpotRid, out var stored));
        Assert.Same(reply, stored);
        var wrongActorCommit = JoinRequest("other-actor");
        Assert.Throws<InvalidOperationException>(() =>
            admissions.BeginCommit(wrongActorCommit, targetSpotRid));
        Assert.Throws<InvalidOperationException>(() =>
            admissions.BeginCommit(JoinRequest("actor-1") with { SourceNodeRid = [9] }, targetSpotRid));
        Assert.Throws<InvalidOperationException>(() =>
            admissions.BeginCommit(JoinRequest("actor-1"), RoutingId.From("spot-other")));

        time.Advance(TimeSpan.FromSeconds(6));
        Assert.Throws<TimeoutException>(() =>
            admissions.BeginCommit(JoinRequest("actor-1"), targetSpotRid));
        Assert.False(admissions.TryGetReply(request, targetSpotRid, out _));

        ZLinkRemoteActorJoinRequest JoinRequest(string actorId) => new(
            actorId,
            "warrior",
            "handoff-1",
            null,
            null,
            "application/json",
            [],
            "application/json",
            [],
            [],
            request.SourceSpotRid,
            request.SourceNodeRid);
    }

    [Fact]
    public async Task ConcurrentAdmission_RunsTheApplicationDecisionOnce()
    {
        var time = new ManualTimeProvider();
        var admissions = new ZLinkActorHandoffAdmissions(time);
        var request = AdmissionRequest(time, "handoff-1");
        var targetSpot = RoutingId.From("spot-1");
        var entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var calls = 0;

        async ValueTask<ZLinkRemoteActorAdmissionReply> Decide(CancellationToken cancellationToken)
        {
            Interlocked.Increment(ref calls);
            entered.TrySetResult();
            await release.Task.WaitAsync(cancellationToken);
            return new ZLinkRemoteActorAdmissionReply(true, "application/json", [], request.DeadlineUnixTimeMilliseconds);
        }

        var first = admissions.AdmitAsync(request, targetSpot, Decide, CancellationToken.None).AsTask();
        await entered.Task;
        var second = admissions.AdmitAsync(request, targetSpot, Decide, CancellationToken.None).AsTask();
        release.TrySetResult();

        var replies = await Task.WhenAll(first, second);
        Assert.Equal(1, calls);
        Assert.Same(replies[0], replies[1]);
    }

    [Fact]
    public async Task Drain_Waits_For_InProgress_And_Accepted_Handoff_Admission()
    {
        var time = new ManualTimeProvider();
        var admissions = new ZLinkActorHandoffAdmissions(time);
        var request = AdmissionRequest(time, "handoff-drain");
        var target = RoutingId.From("target-spot");
        var decisionStarted = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var decision = new TaskCompletionSource<ZLinkRemoteActorAdmissionReply>(
            TaskCreationOptions.RunContinuationsAsynchronously);

        var admission = admissions.AdmitAsync(
            request,
            target,
            _ =>
            {
                decisionStarted.TrySetResult();
                return new ValueTask<ZLinkRemoteActorAdmissionReply>(decision.Task);
            },
            CancellationToken.None).AsTask();
        await decisionStarted.Task;
        var drainSafe = admissions.WaitUntilDrainSafeAsync(CancellationToken.None);
        Assert.False(drainSafe.IsCompleted);

        decision.SetResult(new ZLinkRemoteActorAdmissionReply(
            true,
            "application/json",
            [],
            request.DeadlineUnixTimeMilliseconds));
        await admission;
        Assert.False(drainSafe.IsCompleted);

        admissions.Complete(request.HandoffId);
        await drainSafe.WaitAsync(TimeSpan.FromSeconds(1));
    }

    [Fact]
    public async Task Drain_Rejects_New_Admission_But_Allows_An_Already_Accepted_Commit()
    {
        var time = new ManualTimeProvider();
        var admissions = new ZLinkActorHandoffAdmissions(time);
        var gate = new ZLinkDrainAdmissionGate();
        var request = AdmissionRequest(time, "handoff-accepted-before-drain");
        var target = RoutingId.From("target-spot");

        Assert.True(gate.TryEnterActorAdmission(out var admissionLease));
        var reply = await admissions.AdmitAsync(
            request,
            target,
            _ => ValueTask.FromResult(new ZLinkRemoteActorAdmissionReply(
                true,
                "application/json",
                [],
                request.DeadlineUnixTimeMilliseconds)),
            CancellationToken.None);
        admissionLease.Dispose();
        Assert.True(reply.Accepted);

        Assert.True(gate.BeginDrain());
        Assert.False(gate.TryEnterActorAdmission(out var rejectedAdmission));
        rejectedAdmission.Dispose();

        var commit = CommitRequest(request.HandoffId, []);
        admissions.BeginCommit(commit, target);
        admissions.Complete(request.HandoffId);

        await admissions.WaitUntilDrainSafeAsync(CancellationToken.None)
            .WaitAsync(TimeSpan.FromSeconds(1));
    }

    [Fact]
    public void TerminalHandoffOutcome_SurvivesAdmissionCleanup_AndRejectsChangedRetry()
    {
        var admissions = new ZLinkActorHandoffAdmissions();
        var request = CommitRequest("handoff-1", []);
        var targetSpot = RoutingId.From("spot-1");
        var reply = ZLinkRemoteActorJoinPackets.CreateJoinReply(true, ActorRef("node-b", 2));

        admissions.RecordJoinOutcome(request, targetSpot, reply);
        admissions.Abort(request.HandoffId);

        Assert.True(admissions.TryGetJoinOutcome(request, targetSpot, out var stored));
        Assert.Same(reply, stored);
        Assert.False(admissions.TryGetJoinOutcome(
            request with { TransferState = [9] },
            targetSpot,
            out _));
        var completion = new ZLinkRemoteActorHandoffCompletionRequest(
            request.ActorId,
            request.HandoffId,
            request.SourceSpotRid,
            request.SourceNodeRid,
            targetSpot.ToBytes().ToArray(),
            []);
        Assert.True(admissions.TryBeginCompletion(completion, targetSpot));
        admissions.CancelCompletion(completion, targetSpot);
        Assert.Throws<InvalidOperationException>(() =>
            admissions.TryBeginCompletion(
                completion with
                {
                    Frames =
                    [
                        new ZLinkActorHandoffFrame([], 0, [], [], 1, 0, [], [], 0)
                    ]
                },
                targetSpot));
        Assert.True(admissions.TryBeginCompletion(completion, targetSpot));
        admissions.RecordCompletion(completion, targetSpot);
        Assert.False(admissions.TryBeginCompletion(completion, targetSpot));
        // A completion this target no longer honors is terminal for the
        // source's reconciliation (RequestRejected), never retried.
        var changedTarget = Assert.Throws<ZLinkFrameworkException>(() =>
            admissions.TryBeginCompletion(
                completion with { TargetSpotRid = RoutingId.From("spot-other").ToBytes().ToArray() },
                targetSpot));
        Assert.Equal(ZLinkFrameworkErrorKind.RequestRejected, changedTarget.Kind);
    }

    [Fact]
    public void ActiveTerminalOutcomes_AreNotEvictedByTheCompletedResultCapacity()
    {
        var admissions = new ZLinkActorHandoffAdmissions();
        var targetSpot = RoutingId.From("spot-1");
        var first = CommitRequest("handoff-0", []);
        for (var index = 0; index < 1025; index++)
        {
            var request = CommitRequest($"handoff-{index}", []);
            admissions.RecordJoinOutcome(
                request,
                targetSpot,
                ZLinkRemoteActorJoinPackets.CreateJoinReply(true, ActorRef("node-b", (ulong)index + 1)));
        }

        Assert.True(admissions.TryGetJoinOutcome(first, targetSpot, out _));
    }

    [Fact]
    public void PreparedCommit_ExpiresToRejectedOutcome_AndRejectsLateCompletion()
    {
        var time = new ManualTimeProvider();
        var admissions = new ZLinkActorHandoffAdmissions(time);
        var targetSpot = RoutingId.From("spot-1");
        var request = CommitRequest("handoff-expiring", []);
        var accepted = ZLinkRemoteActorJoinPackets.CreateJoinReply(true, ActorRef("node-b", 2));
        var rejected = ZLinkRemoteActorJoinPackets.CreateJoinReply(false, ActorRef("rejected", 0));
        admissions.RecordJoinOutcome(request, targetSpot, accepted, TimeSpan.FromSeconds(5));

        time.Advance(TimeSpan.FromSeconds(5));

        Assert.True(admissions.TryExpirePreparedCommit(request, targetSpot, rejected));
        Assert.True(admissions.TryGetJoinOutcome(request, targetSpot, out var outcome));
        Assert.False(outcome.Accepted);
        var completion = new ZLinkRemoteActorHandoffCompletionRequest(
            request.ActorId,
            request.HandoffId,
            request.SourceSpotRid,
            request.SourceNodeRid,
            targetSpot.ToBytes().ToArray(),
            []);
        // A completion the target no longer honors is terminal for the
        // source's reconciliation (RequestRejected), never retried.
        var lateCompletion = Assert.Throws<ZLinkFrameworkException>(() =>
            admissions.TryBeginCompletion(completion, targetSpot));
        Assert.Equal(ZLinkFrameworkErrorKind.RequestRejected, lateCompletion.Kind);
    }

    [Fact]
    public void PreparedAcceptance_CanBeAtomicallyCompensatedToRejection()
    {
        var admissions = new ZLinkActorHandoffAdmissions();
        var targetSpot = RoutingId.From("spot-1");
        var request = CommitRequest("handoff-compensated", []);
        var accepted = ZLinkRemoteActorJoinPackets.CreateJoinReply(true, ActorRef("node-b", 2));
        var rejected = ZLinkRemoteActorJoinPackets.CreateJoinReply(false, ActorRef("rejected", 0));

        admissions.RecordJoinOutcome(request, targetSpot, accepted, TimeSpan.FromSeconds(5));
        admissions.RejectPreparedJoinOutcome(request, targetSpot, rejected);

        Assert.True(admissions.TryGetJoinOutcome(request, targetSpot, out var outcome));
        Assert.False(outcome.Accepted);
    }

    [Fact]
    public async Task ExpiredAdmission_IsAtomicallyReplacedByTheNewDecision()
    {
        var time = new ManualTimeProvider();
        var admissions = new ZLinkActorHandoffAdmissions(time);
        var targetSpot = RoutingId.From("spot-1");
        var expired = AdmissionRequest(time, "handoff-1");
        admissions.Register(
            expired,
            targetSpot,
            new ZLinkRemoteActorAdmissionReply(true, "application/json", [1], expired.DeadlineUnixTimeMilliseconds));
        time.Advance(TimeSpan.FromSeconds(6));
        var replacement = AdmissionRequest(time, "handoff-1");
        var calls = 0;

        var reply = await admissions.AdmitAsync(
            replacement,
            targetSpot,
            _ =>
            {
                calls++;
                return ValueTask.FromResult(new ZLinkRemoteActorAdmissionReply(
                    true,
                    "application/json",
                    [2],
                    replacement.DeadlineUnixTimeMilliseconds));
            },
            CancellationToken.None);

        Assert.Equal(1, calls);
        Assert.Equal([2], reply.Reply);
        Assert.True(admissions.TryGetReply(replacement, targetSpot, out var stored));
        Assert.Same(reply, stored);
    }

    private static void Capture(
        ZLinkActorRuntimeState state,
        string bodyText,
        string sessionRid,
        ZlinkStreamMessageKind kind = ZlinkStreamMessageKind.Send,
        ulong requestId = 1,
        uint flags = 0)
    {
        using var body = Message.From(System.Text.Encoding.UTF8.GetBytes(bodyText));
        using var frame = Frame(body, ActorRef("node-a", 1), sessionRid, kind, requestId, flags);

        Assert.True(state.Handoff.TryCapture(frame));
    }

    private static ZLinkSpotActorFrame Frame(
        Message body,
        ZLinkBackendActorRef actor,
        string sessionRid,
        ZlinkStreamMessageKind kind = ZlinkStreamMessageKind.Send,
        ulong requestId = 1,
        uint flags = 0)
    {
        return new ZLinkSpotActorFrame(
            actor,
            actor,
            RoutingId.From("session-node"),
            RoutingId.From(sessionRid),
            requestId,
            flags,
            new ZlinkStreamHeader(
                kind,
                ZlinkStreamCodec.Raw,
                kind == ZlinkStreamMessageKind.Request
                    ? ZlinkStreamHeaderFlags.HasRequestSeq
                    : ZlinkStreamHeaderFlags.None,
                kind == ZlinkStreamMessageKind.Request ? new ZlinkStreamRequestSeq(42) : null,
                "Packet",
                ZlinkStreamMetadata.Empty),
            Message.From(body.AsReadOnlySpan()));
    }

    private static string DecodeBody(ZLinkActorHandoffFrame frame)
        => System.Text.Encoding.UTF8.GetString(frame.Body);

    private static ZlinkStreamHeader Header(string packetName)
        => new(
            ZlinkStreamMessageKind.Send,
            ZlinkStreamCodec.Raw,
            ZlinkStreamHeaderFlags.None,
            null,
            packetName,
            ZlinkStreamMetadata.Empty);

    private static ZLinkBackendActorRef ActorRef(string nodeRid, ulong generation)
        => new(RoutingId.From(nodeRid), "actor-1", generation);

    private static ZLinkRemoteActorJoinRequest CommitRequest(
        string handoffId,
        IReadOnlyList<ZLinkActorHandoffFrame> frames)
        => new(
            "actor-1",
            "warrior",
            handoffId,
            null,
            null,
            "application/json",
            [],
            "application/json",
            [],
            frames,
            [1],
            [2]);

    private static ZLinkRemoteActorAdmissionRequest AdmissionRequest(
        TimeProvider timeProvider,
        string handoffId)
        => new(
            "actor-1",
            "warrior",
            [1],
            [2],
            "application/json",
            [],
            handoffId,
            (timeProvider.GetUtcNow() + TimeSpan.FromSeconds(5)).ToUnixTimeMilliseconds());

    private sealed class ActorWithoutTransferAdapter : IZLinkActor
    {
        public string ActorId => "actor-without-adapter";

        public IZLinkActorContext Context => throw new InvalidOperationException(
            "Empty-state capture must not inspect the actor context.");
    }
}
