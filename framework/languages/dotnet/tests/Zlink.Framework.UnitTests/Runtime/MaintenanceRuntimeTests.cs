using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Runtime.Host;

namespace Zlink.Framework.UnitTests;

public sealed class MaintenanceRuntimeTests
{
    [Fact]
    public async Task Framework_Registration_Exposes_Host_Wide_Maintenance_Runtime()
    {
        var registrations = new ServiceCollection();
        registrations.AddZLinkFramework(_ => { });
        await using var services = registrations.BuildServiceProvider();

        var runtime = services.GetRequiredService<IZLinkFrameworkRuntime>();
        var result = await runtime.ShutdownAsync(TimeSpan.FromSeconds(1));

        Assert.Equal(ZLinkFrameworkTerminationIntent.Shutdown, result.EffectiveIntent);
        Assert.Equal(ZLinkFrameworkTerminationOutcome.Stopped, result.Outcome);
        Assert.Equal(ZLinkFrameworkRuntimeState.Stopped, runtime.State);
    }

    [Fact]
    public async Task Retire_Preflight_Block_Does_Not_Seal_And_Can_Retry()
    {
        var attempts = 0;
        using var fixture = Create(
            _ => ValueTask.FromResult<ZLinkFrameworkTerminationReason?>(
                Interlocked.Increment(ref attempts) == 1
                    ? ZLinkFrameworkTerminationReason.TargetUnavailable
                    : null));
        fixture.Runtime.MarkServing();

        var blocked = await fixture.Runtime.RetireAsync();

        Assert.Equal(ZLinkFrameworkTerminationOutcome.Blocked, blocked.Outcome);
        Assert.Equal(ZLinkFrameworkTerminationReason.TargetUnavailable, blocked.Reason);
        Assert.Equal(ZLinkFrameworkRuntimeState.Serving, fixture.Runtime.State);
        Assert.False(fixture.Runtime.Snapshot().WorkSealed);
        Assert.Equal(0, fixture.Executor.ExecuteCount);

        fixture.Executor.Complete.TrySetResult(null);
        var completed = await fixture.Runtime.RetireAsync();

        Assert.Equal(ZLinkFrameworkTerminationIntent.Retire, completed.EffectiveIntent);
        Assert.Equal(ZLinkFrameworkTerminationOutcome.Stopped, completed.Outcome);
        Assert.Equal(1, fixture.Executor.ExecuteCount);
        Assert.Equal(
            ZLinkFrameworkTerminationIntent.Retire,
            fixture.Executor.Intent);
    }

    [Fact]
    public async Task Shutdown_During_Preflight_Wins_The_Maintenance_Barrier()
    {
        var preflight = new TaskCompletionSource<ZLinkFrameworkTerminationReason?>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        using var fixture = Create(
            cancellationToken => new ValueTask<ZLinkFrameworkTerminationReason?>(
                preflight.Task.WaitAsync(cancellationToken)));
        fixture.Runtime.MarkServing();

        var retire = fixture.Runtime.RetireAsync().AsTask();
        var shutdown = fixture.Runtime.ShutdownAsync().AsTask();
        preflight.TrySetResult(null);
        fixture.Executor.Complete.TrySetResult(null);

        var first = await retire;
        var second = await shutdown;
        Assert.Equal(ZLinkFrameworkTerminationIntent.Shutdown, first.EffectiveIntent);
        Assert.Equal(first, second);
        Assert.Equal(1, fixture.Executor.ExecuteCount);
        Assert.Equal(
            ZLinkFrameworkTerminationIntent.Shutdown,
            fixture.Executor.Intent);
    }

    [Fact]
    public async Task Shutdown_After_Retiring_Publication_Joins_Retire()
    {
        using var fixture = Create();
        fixture.Runtime.MarkServing();

        var retire = fixture.Runtime.RetireAsync().AsTask();
        await fixture.Executor.Started.Task.WaitAsync(TimeSpan.FromSeconds(1));
        var shutdown = fixture.Runtime.ShutdownAsync().AsTask();
        fixture.Executor.Complete.TrySetResult(null);

        var first = await retire;
        var second = await shutdown;
        Assert.Equal(ZLinkFrameworkTerminationIntent.Retire, first.EffectiveIntent);
        Assert.Equal(first, second);
    }

    [Fact]
    public async Task Caller_Cancellation_Ends_Only_That_Waiter()
    {
        using var fixture = Create();
        fixture.Runtime.MarkServing();
        var shared = fixture.Runtime.ShutdownAsync().AsTask();
        using var waiterCancellation = new CancellationTokenSource();
        var canceledWaiter = fixture.Runtime.ShutdownAsync(
            cancellationToken: waiterCancellation.Token).AsTask();

        waiterCancellation.Cancel();
        await Assert.ThrowsAnyAsync<OperationCanceledException>(() => canceledWaiter);
        Assert.False(shared.IsCompleted);

        fixture.Executor.Complete.TrySetResult(null);
        var result = await shared;
        Assert.Equal(ZLinkFrameworkTerminationOutcome.Stopped, result.Outcome);
    }

    [Fact]
    public async Task Shutdown_Force_Stop_Has_One_Terminal_Result()
    {
        using var fixture = Create();
        fixture.Executor.Complete.TrySetResult(ZLinkDrainForceReason.DeadlineExceeded);

        var result = await fixture.Runtime.ShutdownAsync();
        var repeated = await fixture.Runtime.RetireAsync();

        Assert.Equal(ZLinkFrameworkTerminationIntent.Shutdown, result.EffectiveIntent);
        Assert.Equal(ZLinkFrameworkTerminationOutcome.ForceStopped, result.Outcome);
        Assert.Equal(ZLinkFrameworkTerminationReason.DeadlineExceeded, result.Reason);
        Assert.Equal(result, repeated);
        Assert.Equal(result, fixture.Runtime.Snapshot().TerminalResult);
    }

    [Fact]
    public async Task Observation_Is_Ordered_And_Terminal_State_Is_Visible()
    {
        using var fixture = Create();
        await using var observer = fixture.Runtime.ObserveAsync(capacity: 2).GetAsyncEnumerator();
        var next = observer.MoveNextAsync().AsTask();

        fixture.Runtime.MarkServing();

        Assert.True(await next);
        Assert.Equal("zlink.runtime.host.termination_changed", observer.Current.Identifier);
        Assert.Equal(ZLinkFrameworkRuntimeState.Serving, observer.Current.State);
        Assert.Equal<ulong>(1, observer.Current.Sequence);

        fixture.Executor.Complete.TrySetResult(null);
        await fixture.Runtime.ShutdownAsync();
        var snapshot = fixture.Runtime.Snapshot();
        Assert.Equal(ZLinkFrameworkRuntimeState.Stopped, snapshot.State);
        Assert.True(snapshot.WorkSealed);
        Assert.True(snapshot.Sequence >= 4);
    }

    [Fact]
    public async Task Capacity_One_Observer_Retains_Terminal_Event()
    {
        using var fixture = Create();
        await using var observer = fixture.Runtime.ObserveAsync(capacity: 1).GetAsyncEnumerator();
        var first = observer.MoveNextAsync().AsTask();
        fixture.Runtime.MarkServing();
        Assert.True(await first);

        fixture.Executor.Complete.TrySetResult(null);
        await fixture.Runtime.ShutdownAsync();

        Assert.True(await observer.MoveNextAsync());
        Assert.Equal(
            ZLinkFrameworkTerminationOutcome.Stopped,
            observer.Current.Outcome);
        Assert.Equal(ZLinkFrameworkRuntimeState.Stopped, observer.Current.State);
    }

    [Fact]
    public void Snapshot_Reports_Framework_Owned_Pending_Counts()
    {
        using var fixture = Create(
            pending: static () => new ZLinkDrainRemainderCounts(
                Actors: 2,
                Spots: 3,
                Requests: 5,
                Sessions: 7));

        var snapshot = fixture.Runtime.Snapshot();

        Assert.Equal<ulong>(5, snapshot.PendingRequestCount);
        Assert.Equal<ulong>(5, snapshot.PendingRelocationCount);
        Assert.Equal<ulong>(7, snapshot.PendingStreamBarrierCount);
    }

    private static Fixture Create(
        Func<CancellationToken, ValueTask<ZLinkFrameworkTerminationReason?>>? preflight = null,
        Func<ZLinkDrainRemainderCounts>? pending = null)
    {
        var executor = new MaintenanceExecutor();
        var drain = new ZLinkDrainCoordinator(
            new ZLinkDrainAdmissionGate(),
            executor,
            events: null);
        var runtime = new ZLinkFrameworkMaintenanceRuntime(
            drain,
            preflight ?? (static _ =>
                ValueTask.FromResult<ZLinkFrameworkTerminationReason?>(null)),
            pending ?? (static () => new ZLinkDrainRemainderCounts(0, 0, 0, 0)));
        return new Fixture(runtime, drain, executor);
    }

    private sealed record Fixture(
        ZLinkFrameworkMaintenanceRuntime Runtime,
        ZLinkDrainCoordinator Drain,
        MaintenanceExecutor Executor) : IDisposable
    {
        public void Dispose()
        {
            Runtime.Dispose();
            Drain.Dispose();
        }
    }

    private sealed class MaintenanceExecutor : IZLinkDrainExecutor
    {
        public TaskCompletionSource<ZLinkDrainForceReason?> Complete { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource Started { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public int ExecuteCount { get; private set; }

        public ZLinkFrameworkTerminationIntent? Intent { get; private set; }

        public async ValueTask<ZLinkDrainForceReason?> ExecuteAsync(
            TimeSpan deadline,
            CancellationToken deadlineToken)
        {
            return await ExecuteAsync(
                    ZLinkFrameworkTerminationIntent.Shutdown,
                    deadline,
                    deadlineToken)
                .ConfigureAwait(false);
        }

        public async ValueTask<ZLinkDrainForceReason?> ExecuteAsync(
            ZLinkFrameworkTerminationIntent intent,
            TimeSpan deadline,
            CancellationToken deadlineToken)
        {
            _ = deadline;
            Intent = intent;
            ExecuteCount++;
            Started.TrySetResult();
            return await Complete.Task.WaitAsync(deadlineToken).ConfigureAwait(false);
        }

        public ValueTask ForceStopAsync(
            ZLinkDrainForceReason reason,
            CancellationToken cancellationToken)
        {
            _ = reason;
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.CompletedTask;
        }
    }
}
