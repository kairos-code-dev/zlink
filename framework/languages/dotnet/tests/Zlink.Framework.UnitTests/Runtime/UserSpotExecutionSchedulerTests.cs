using System.Collections.Concurrent;

namespace Zlink.Framework.UnitTests;

public sealed class UserSpotExecutionSchedulerTests
{
    [Fact]
    public void FactoryRegistrationFixesExecutionModeAndRejectsInvalidValues()
    {
        var registration = new ZLinkSpotNodeRegistration
        {
            SpotNodeName = "execution-node"
        };
        IZLinkMeshObjectServerBuilder server = new ZLinkMeshNodeBuilder(registration);

        server.AddSpotFactory<ExecutionTestSpot>(
            "execution.spot",
            new ZLinkUserSpotFactoryOptions
            {
                StableTypeLimit = 8,
                ExecutionMode = ZLinkUserSpotExecutionMode.PerActor
            },
            ZLinkRelocationPolicy<ExecutionTestSpot>.Disabled);

        var configured = registration.UserSpotFactoryOptions[typeof(ExecutionTestSpot)];
        Assert.Equal(8, configured.StableTypeLimit);
        Assert.Equal(ZLinkUserSpotExecutionMode.PerActor, configured.ExecutionMode);

        var invalidRegistration = new ZLinkSpotNodeRegistration
        {
            SpotNodeName = "invalid-execution-node"
        };
        IZLinkMeshObjectServerBuilder invalid = new ZLinkMeshNodeBuilder(invalidRegistration);
        Assert.Throws<ZLinkConfigurationException>(() =>
            invalid.AddSpotFactory<OtherExecutionTestSpot>(
                "invalid.execution.spot",
                new ZLinkUserSpotFactoryOptions
                {
                    ExecutionMode = (ZLinkUserSpotExecutionMode)9
                },
                ZLinkRelocationPolicy<OtherExecutionTestSpot>.Disabled));
    }

    [Fact]
    public async Task SpotWide_Yield_ReleasesSpotGateButKeepsActorFifoClaim()
    {
        using var errorSink = new ZLinkRuntimeErrorSink();
        await using var executor = CreateExecutor(errorSink, ZLinkUserSpotExecutionMode.SpotWide);
        var externalStarted = NewSignal();
        var completeExternal = NewSignal();
        var firstResumed = NewSignal();
        var sameActorSecondRan = NewSignal();
        var otherActorRan = NewSignal();
        var spotRan = NewSignal();
        var order = new ConcurrentQueue<string>();

        var first = executor.ExecuteActorAsync(
            "actor-1",
            async static (_, state, ct) =>
            {
                state.Order.Enqueue("actor-1-start");
                var turn = ZLinkApplicationExecutionContext.RequireYieldTurn("test request");
                await turn.YieldFrameworkCallAsync(
                        async _ =>
                        {
                            state.ExternalStarted.TrySetResult();
                            await state.CompleteExternal.Task.ConfigureAwait(false);
                        },
                        ct)
                    .ConfigureAwait(false);
                state.Order.Enqueue("actor-1-resumed");
                state.FirstResumed.TrySetResult();
            },
            new YieldState(order, externalStarted, completeExternal, firstResumed),
            CancellationToken.None).AsTask();
        await externalStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));

        var sameActorSecond = RecordActor(
            executor,
            "actor-1",
            order,
            "actor-1-second",
            sameActorSecondRan);
        var otherActor = RecordActor(executor, "actor-2", order, "actor-2", otherActorRan);
        var spot = executor.ExecuteAsync(
            (_, _) =>
            {
                order.Enqueue("spot");
                spotRan.TrySetResult();
                return ValueTask.CompletedTask;
            },
            CancellationToken.None).AsTask();

        await Task.WhenAll(otherActorRan.Task, spotRan.Task).WaitAsync(TimeSpan.FromSeconds(5));
        Assert.False(sameActorSecondRan.Task.IsCompleted);

        completeExternal.TrySetResult();
        await Task.WhenAll(first, sameActorSecond, otherActor, spot)
            .WaitAsync(TimeSpan.FromSeconds(5));
        Assert.True(firstResumed.Task.IsCompleted);
        var recorded = order.ToArray();
        Assert.True(
            Array.IndexOf(recorded, "actor-1-resumed")
            < Array.IndexOf(recorded, "actor-1-second"));
    }

    [Fact]
    public async Task PerActor_UsesIndependentActorSpotAndTimerLanesWithLaneFifo()
    {
        using var errorSink = new ZLinkRuntimeErrorSink();
        await using var executor = CreateExecutor(errorSink, ZLinkUserSpotExecutionMode.PerActor);
        var actorOneStarted = NewSignal();
        var releaseActorOne = NewSignal();
        var actorOneSecondRan = NewSignal();
        var actorTwoRan = NewSignal();
        var spotRan = NewSignal();
        var timerOneStarted = NewSignal();
        var releaseTimerOne = NewSignal();
        var timerOneSecondRan = NewSignal();
        var timerTwoRan = NewSignal();

        var actorOne = executor.ExecuteActorAsync(
            "actor-1",
            async static (_, state, _) =>
            {
                state.Started.TrySetResult();
                await state.Release.Task.ConfigureAwait(false);
            },
            new BlockState(actorOneStarted, releaseActorOne),
            CancellationToken.None).AsTask();
        var timerOne = executor.ExecuteTimerAsync(
            "timer-1",
            async static (_, state, _) =>
            {
                state.Started.TrySetResult();
                await state.Release.Task.ConfigureAwait(false);
            },
            new BlockState(timerOneStarted, releaseTimerOne),
            CancellationToken.None).AsTask();
        await Task.WhenAll(actorOneStarted.Task, timerOneStarted.Task)
            .WaitAsync(TimeSpan.FromSeconds(5));

        var actorOneSecond = RecordActor(
            executor,
            "actor-1",
            null,
            null,
            actorOneSecondRan);
        var actorTwo = RecordActor(executor, "actor-2", null, null, actorTwoRan);
        var spot = executor.ExecuteAsync(
            (_, _) =>
            {
                spotRan.TrySetResult();
                return ValueTask.CompletedTask;
            },
            CancellationToken.None).AsTask();
        var timerOneSecond = RecordTimer(executor, "timer-1", timerOneSecondRan);
        var timerTwo = RecordTimer(executor, "timer-2", timerTwoRan);

        await Task.WhenAll(actorTwoRan.Task, spotRan.Task, timerTwoRan.Task)
            .WaitAsync(TimeSpan.FromSeconds(5));
        Assert.False(actorOneSecondRan.Task.IsCompleted);
        Assert.False(timerOneSecondRan.Task.IsCompleted);

        releaseActorOne.TrySetResult();
        releaseTimerOne.TrySetResult();
        await Task.WhenAll(
                actorOne,
                actorOneSecond,
                actorTwo,
                spot,
                timerOne,
                timerOneSecond,
                timerTwo)
            .WaitAsync(TimeSpan.FromSeconds(5));
    }

    [Fact]
    public async Task PerActor_YieldIsRejectedBeforeOperationSubmission()
    {
        using var errorSink = new ZLinkRuntimeErrorSink();
        await using var executor = CreateExecutor(errorSink, ZLinkUserSpotExecutionMode.PerActor);
        var submitCount = 0;

        var failure = await Assert.ThrowsAsync<ZLinkFrameworkException>(
            () => executor.ExecuteActorAsync(
                    "actor-1",
                    (_, _, _) =>
                    {
                        _ = ZLinkApplicationExecutionContext.RequireYieldTurn("Actor request");
                        Interlocked.Increment(ref submitCount);
                        return ValueTask.CompletedTask;
                    },
                    0,
                    CancellationToken.None)
                .AsTask());

        Assert.Equal(ZLinkFrameworkErrorKind.InvalidConfiguration, failure.Kind);
        Assert.Equal(0, Volatile.Read(ref submitCount));
    }

    [Fact]
    public async Task SpotWide_SameGateAwaitIsRejectedBeforeSubmission()
    {
        using var errorSink = new ZLinkRuntimeErrorSink();
        await using var executor = CreateExecutor(errorSink, ZLinkUserSpotExecutionMode.SpotWide);
        var attempts = 0;

        await executor.ExecuteActorAsync(
            "actor-1",
            (_, _, _) =>
            {
                AssertInvalid(() =>
                {
                    ZLinkApplicationExecutionContext.RejectActorRequestWhenSameClaim("actor-1");
                    Interlocked.Increment(ref attempts);
                });
                using (ZLinkApplicationExecutionContext.Push(
                           new ZLinkApplicationExecutionScope(
                               "test-spot",
                               ZLinkUserSpotExecutionMode.SpotWide,
                               "actor-1",
                               YieldAllowed: true,
                               IsMemberActor: candidate => candidate == "actor-2")))
                    AssertInvalid(() =>
                    {
                        ZLinkApplicationExecutionContext
                            .RejectActorRequestWhenSameClaim("actor-2");
                        Interlocked.Increment(ref attempts);
                    });
                AssertInvalid(() =>
                {
                    ZLinkApplicationExecutionContext.RejectSpotRequestWhenSameGate("test-spot");
                    Interlocked.Increment(ref attempts);
                });
                AssertInvalid(() =>
                {
                    ZLinkApplicationExecutionContext.RejectActorJoinWhenSameGate("another-spot");
                    Interlocked.Increment(ref attempts);
                });
                return ValueTask.CompletedTask;
            },
            0,
            CancellationToken.None);

        Assert.Equal(0, Volatile.Read(ref attempts));
    }

    [Fact]
    public async Task PerActor_RelocationSeal_WaitsForEveryActorSpotAndTimerLane()
    {
        using var errorSink = new ZLinkRuntimeErrorSink();
        await using var executor = CreateExecutor(
            errorSink,
            ZLinkUserSpotExecutionMode.PerActor);
        var actorStarted = NewSignal();
        var releaseActor = NewSignal();
        var spotStarted = NewSignal();
        var releaseSpot = NewSignal();
        var timerStarted = NewSignal();
        var releaseTimer = NewSignal();

        var actor = executor.ExecuteActorAsync(
            "actor-1",
            Block,
            new BlockState(actorStarted, releaseActor),
            CancellationToken.None).AsTask();
        var spot = executor.ExecuteAsync(
            Block,
            new BlockState(spotStarted, releaseSpot),
            CancellationToken.None).AsTask();
        var timer = executor.ExecuteTimerAsync(
            "timer-1",
            Block,
            new BlockState(timerStarted, releaseTimer),
            CancellationToken.None).AsTask();
        await Task.WhenAll(actorStarted.Task, spotStarted.Task, timerStarted.Task)
            .WaitAsync(TimeSpan.FromSeconds(5));

        var sealTask = executor.SealRelocationAsync(CancellationToken.None).AsTask();
        Assert.False(sealTask.IsCompleted);

        releaseSpot.TrySetResult();
        await spot.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.False(sealTask.IsCompleted);

        releaseActor.TrySetResult();
        await actor.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.False(sealTask.IsCompleted);

        releaseTimer.TrySetResult();
        await timer.WaitAsync(TimeSpan.FromSeconds(5));
        var seal = await sealTask.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.True(executor.TryAbortRelocation(seal));

        static async ValueTask Block(
            ZLinkSpotActivation _,
            BlockState state,
            CancellationToken __)
        {
            state.Started.TrySetResult();
            await state.Release.Task.ConfigureAwait(false);
        }
    }

    [Fact]
    public async Task SpotWide_RelocationSeal_WaitsForYieldedTerminalContinuation()
    {
        using var errorSink = new ZLinkRuntimeErrorSink();
        await using var executor = CreateExecutor(
            errorSink,
            ZLinkUserSpotExecutionMode.SpotWide);
        var externalStarted = NewSignal();
        var completeExternal = NewSignal();
        var terminalContinuation = NewSignal();

        var operation = executor.ExecuteActorAsync(
            "actor-1",
            async static (_, state, ct) =>
            {
                var turn = ZLinkApplicationExecutionContext
                    .RequireYieldTurn("relocation barrier test");
                await turn.YieldFrameworkCallAsync(
                        async _ =>
                        {
                            state.ExternalStarted.TrySetResult();
                            await state.CompleteExternal.Task.ConfigureAwait(false);
                        },
                        ct)
                    .ConfigureAwait(false);
                state.TerminalContinuation.TrySetResult();
            },
            new YieldBarrierState(
                externalStarted,
                completeExternal,
                terminalContinuation),
            CancellationToken.None).AsTask();
        await externalStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));

        var sealTask = executor.SealRelocationAsync(CancellationToken.None).AsTask();
        Assert.False(sealTask.IsCompleted);

        completeExternal.TrySetResult();
        await terminalContinuation.Task.WaitAsync(TimeSpan.FromSeconds(5));
        var seal = await sealTask.WaitAsync(TimeSpan.FromSeconds(5));
        await operation.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.True(executor.TryAbortRelocation(seal));
    }

    [Fact]
    public async Task RelocationAbort_OnlyReopensItsOwnBarrierGeneration()
    {
        using var errorSink = new ZLinkRuntimeErrorSink();
        await using var executor = CreateExecutor(
            errorSink,
            ZLinkUserSpotExecutionMode.PerActor);

        var first = await executor.SealRelocationAsync(CancellationToken.None);
        Assert.True(executor.TryAbortRelocation(first));
        var second = await executor.SealRelocationAsync(CancellationToken.None);

        Assert.False(executor.TryAbortRelocation(first));
        Assert.True(executor.TryAbortRelocation(second));

        var ran = NewSignal();
        await RecordActor(executor, "actor-1", null, null, ran)
            .WaitAsync(TimeSpan.FromSeconds(5));
        Assert.True(ran.Task.IsCompleted);
    }

    [Fact]
    public async Task CallerCancellation_DoesNotReleaseRelocationClaimBeforeCallbackEnds()
    {
        using var errorSink = new ZLinkRuntimeErrorSink();
        await using var executor = CreateExecutor(
            errorSink,
            ZLinkUserSpotExecutionMode.PerActor);
        using var callerCancellation = new CancellationTokenSource();
        var started = NewSignal();
        var release = NewSignal();

        var operation = executor.ExecuteActorAsync(
            "actor-1",
            async static (_, state, _) =>
            {
                state.Started.TrySetResult();
                await state.Release.Task.ConfigureAwait(false);
            },
            new BlockState(started, release),
            callerCancellation.Token).AsTask();
        await started.Task.WaitAsync(TimeSpan.FromSeconds(5));

        callerCancellation.Cancel();
        await Assert.ThrowsAnyAsync<OperationCanceledException>(async () =>
            await operation.ConfigureAwait(false));
        var sealTask = executor.SealRelocationAsync(CancellationToken.None).AsTask();
        Assert.False(sealTask.IsCompleted);

        release.TrySetResult();
        var seal = await sealTask.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.True(executor.TryAbortRelocation(seal));
    }

    [Fact]
    public async Task ClosingBarrier_WaitsForAllLanesAndKeepsAdmissionSealed()
    {
        using var errorSink = new ZLinkRuntimeErrorSink();
        await using var executor = CreateExecutor(
            errorSink,
            ZLinkUserSpotExecutionMode.PerActor);
        var started = NewSignal();
        var release = NewSignal();
        var closingRan = NewSignal();
        var actor = executor.ExecuteActorAsync(
            "actor-1",
            async static (_, state, _) =>
            {
                state.Started.TrySetResult();
                await state.Release.Task.ConfigureAwait(false);
            },
            new BlockState(started, release),
            CancellationToken.None).AsTask();
        await started.Task.WaitAsync(TimeSpan.FromSeconds(5));

        var closing = executor.ExecuteQuiescentLifecycleAsync(
            (_, _) =>
            {
                closingRan.TrySetResult();
                return ValueTask.FromResult(true);
            },
            CancellationToken.None).AsTask();
        Assert.False(closingRan.Task.IsCompleted);

        release.TrySetResult();
        await actor.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.True(await closing.WaitAsync(TimeSpan.FromSeconds(5)));
        Assert.True(closingRan.Task.IsCompleted);

        var failure = await Assert.ThrowsAsync<ZLinkFrameworkException>(() =>
            executor.ExecuteActorAsync(
                    "actor-2",
                    static (_, _, _) => ValueTask.CompletedTask,
                    0,
                    CancellationToken.None)
                .AsTask());
        Assert.Equal(ZLinkFrameworkErrorKind.RequestRejected, failure.Kind);
    }

    [Fact]
    public async Task ClosingBarrier_WaitsForYieldedAcceptedSpotWork()
    {
        using var errorSink = new ZLinkRuntimeErrorSink();
        await using var executor = CreateExecutor(
            errorSink,
            ZLinkUserSpotExecutionMode.SpotWide);
        var externalStarted = NewSignal();
        var releaseExternal = NewSignal();
        var terminal = NewSignal();
        var closingRan = NewSignal();

        Assert.True(executor.QueueAccepted(
            new byte[] { 1 },
            async (_, ct) =>
            {
                var turn = ZLinkApplicationExecutionContext
                    .RequireYieldTurn("accepted close barrier test");
                await turn.YieldFrameworkCallAsync(
                        async _ =>
                        {
                            externalStarted.TrySetResult();
                            await releaseExternal.Task.ConfigureAwait(false);
                        },
                        ct)
                    .ConfigureAwait(false);
                terminal.TrySetResult();
            },
            static () => { },
            out var acceptedCompletion));
        await externalStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));

        var closing = executor.ExecuteQuiescentLifecycleAsync(
            (_, _) =>
            {
                closingRan.TrySetResult();
                return ValueTask.FromResult(true);
            },
            CancellationToken.None).AsTask();
        Assert.False(closingRan.Task.IsCompleted);

        releaseExternal.TrySetResult();
        await terminal.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await acceptedCompletion.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.True(await closing.WaitAsync(TimeSpan.FromSeconds(5)));
        Assert.True(closingRan.Task.IsCompleted);
    }

    private static ZLinkSpotSerialExecutor CreateExecutor(
        IZLinkRuntimeFailureReporter errorSink,
        ZLinkUserSpotExecutionMode mode)
    {
        return new ZLinkSpotSerialExecutor(
            null!,
            static () => false,
            CancellationToken.None,
            errorSink,
            executionMode: mode);
    }

    private static Task RecordActor(
        ZLinkSpotSerialExecutor executor,
        string actorId,
        ConcurrentQueue<string>? order,
        string? marker,
        TaskCompletionSource signal)
    {
        return executor.ExecuteActorAsync(
            actorId,
            static (_, state, _) =>
            {
                if (state.Order is not null && state.Marker is not null)
                    state.Order.Enqueue(state.Marker);
                state.Signal.TrySetResult();
                return ValueTask.CompletedTask;
            },
            new RecordState(order, marker, signal),
            CancellationToken.None).AsTask();
    }

    private static Task RecordTimer(
        ZLinkSpotSerialExecutor executor,
        string timerName,
        TaskCompletionSource signal)
    {
        return executor.ExecuteTimerAsync(
            timerName,
            static (_, completion, _) =>
            {
                completion.TrySetResult();
                return ValueTask.CompletedTask;
            },
            signal,
            CancellationToken.None).AsTask();
    }

    private static void AssertInvalid(Action operation)
    {
        var failure = Assert.Throws<ZLinkFrameworkException>(operation);
        Assert.Equal(ZLinkFrameworkErrorKind.InvalidConfiguration, failure.Kind);
    }

    private static TaskCompletionSource NewSignal() =>
        new(TaskCreationOptions.RunContinuationsAsynchronously);

    private sealed record YieldState(
        ConcurrentQueue<string> Order,
        TaskCompletionSource ExternalStarted,
        TaskCompletionSource CompleteExternal,
        TaskCompletionSource FirstResumed);

    private sealed record RecordState(
        ConcurrentQueue<string>? Order,
        string? Marker,
        TaskCompletionSource Signal);

    private sealed record BlockState(
        TaskCompletionSource Started,
        TaskCompletionSource Release);

    private sealed record YieldBarrierState(
        TaskCompletionSource ExternalStarted,
        TaskCompletionSource CompleteExternal,
        TaskCompletionSource TerminalContinuation);

    private sealed class ExecutionTestSpot(IZLinkSpotContext context) : IZLinkSpot
    {
        public IZLinkSpotContext Context { get; } = context;
    }

    private sealed class OtherExecutionTestSpot(IZLinkSpotContext context) : IZLinkSpot
    {
        public IZLinkSpotContext Context { get; } = context;
    }
}
