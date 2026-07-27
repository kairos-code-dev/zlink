using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.UnitTests;

public sealed class MaintenanceRuntimeTests
{
    [Fact]
    public async Task Planned_maintenance_relocates_without_stopping_the_host()
    {
        using var fixture = Create(sourceApplicationVersion: 7);
        fixture.Runtime.MarkServing();
        fixture.Executor.Complete.TrySetResult(null);

        var result = await fixture.Runtime.RelocateAsync(new ZLinkFrameworkRelocationOptions
        {
            Mode = ZLinkFrameworkRelocationMode.PlannedMaintenance
        });

        Assert.Equal(ZLinkFrameworkRelocationOutcome.Relocated, result.Outcome);
        Assert.Equal(7, result.TargetApplicationVersion);
        Assert.Equal(ZLinkFrameworkRuntimeState.Relocated, fixture.Runtime.Status.State);
        Assert.Null(fixture.Runtime.Status.TerminationResult);
        Assert.Equal(ZLinkFrameworkLifecycleIntent.Relocate, fixture.Executor.Intent);
    }

    [Fact]
    public async Task Shutdown_is_a_separate_operation_after_relocation()
    {
        using var fixture = Create(sourceApplicationVersion: 7);
        fixture.Runtime.MarkServing();
        fixture.Executor.Complete.TrySetResult(null);
        await fixture.Runtime.RelocateAsync(new ZLinkFrameworkRelocationOptions
        {
            Mode = ZLinkFrameworkRelocationMode.PlannedMaintenance
        });

        var result = await fixture.Runtime.ShutdownAsync();

        Assert.Equal(ZLinkFrameworkTerminationOutcome.Stopped, result.Outcome);
        Assert.Equal(ZLinkFrameworkRuntimeState.Stopped, fixture.Runtime.Status.State);
        Assert.Equal(2, fixture.Executor.ExecuteCount);
        Assert.Equal(ZLinkFrameworkLifecycleIntent.Shutdown, fixture.Executor.Intent);
    }

    [Fact]
    public async Task Rolling_update_requires_a_newer_exact_target_version()
    {
        using var fixture = Create(sourceApplicationVersion: 7);
        fixture.Runtime.MarkServing();

        await Assert.ThrowsAsync<ArgumentException>(() =>
            fixture.Runtime.RelocateAsync(new ZLinkFrameworkRelocationOptions
            {
                Mode = ZLinkFrameworkRelocationMode.RollingUpdate,
                TargetApplicationVersion = 7
            }).AsTask());
        await Assert.ThrowsAsync<ArgumentException>(() =>
            fixture.Runtime.RelocateAsync(new ZLinkFrameworkRelocationOptions
            {
                Mode = ZLinkFrameworkRelocationMode.PlannedMaintenance,
                TargetApplicationVersion = 8
            }).AsTask());
    }

    [Fact]
    public async Task Preflight_blocker_keeps_the_host_serving()
    {
        using var fixture = Create(
            static (_, _, _) => ValueTask.FromResult<ZLinkFrameworkRelocationReason?>(
                ZLinkFrameworkRelocationReason.TargetUnavailable));
        fixture.Runtime.MarkServing();

        var result = await fixture.Runtime.RelocateAsync(new ZLinkFrameworkRelocationOptions
        {
            Mode = ZLinkFrameworkRelocationMode.PlannedMaintenance
        });

        Assert.Equal(ZLinkFrameworkRelocationOutcome.Blocked, result.Outcome);
        Assert.Equal(ZLinkFrameworkRelocationReason.TargetUnavailable, result.Reason);
        Assert.Equal(ZLinkFrameworkRuntimeState.Serving, fixture.Runtime.Status.State);
        Assert.Null(fixture.Runtime.Status.RelocationResult);
    }

    [Theory]
    [InlineData(ZLinkFrameworkRelocationMode.PlannedMaintenance, null, 7L)]
    [InlineData(ZLinkFrameworkRelocationMode.RollingUpdate, 9L, 9L)]
    public async Task Preflight_receives_mode_and_exact_effective_target_version(
        ZLinkFrameworkRelocationMode mode,
        long? requestedVersion,
        long expectedVersion)
    {
        ZLinkFrameworkRelocationMode? observedMode = null;
        long? observedVersion = null;
        using var fixture = Create(
            (candidateMode, candidateVersion, _) =>
            {
                observedMode = candidateMode;
                observedVersion = candidateVersion;
                return ValueTask.FromResult<ZLinkFrameworkRelocationReason?>(
                    ZLinkFrameworkRelocationReason.TargetUnavailable);
            },
            sourceApplicationVersion: 7);
        fixture.Runtime.MarkServing();

        var result = await fixture.Runtime.RelocateAsync(
            new ZLinkFrameworkRelocationOptions
            {
                Mode = mode,
                TargetApplicationVersion = requestedVersion
            });

        Assert.Equal(mode, observedMode);
        Assert.Equal(expectedVersion, observedVersion);
        Assert.Equal(ZLinkFrameworkRelocationReason.TargetUnavailable, result.Reason);
    }

    [Theory]
    [InlineData(ZLinkFrameworkRelocationMode.PlannedMaintenance, 7, 6, false)]
    [InlineData(ZLinkFrameworkRelocationMode.PlannedMaintenance, 7, 7, true)]
    [InlineData(ZLinkFrameworkRelocationMode.PlannedMaintenance, 7, 8, false)]
    [InlineData(ZLinkFrameworkRelocationMode.RollingUpdate, 9, 8, false)]
    [InlineData(ZLinkFrameworkRelocationMode.RollingUpdate, 9, 9, true)]
    [InlineData(ZLinkFrameworkRelocationMode.RollingUpdate, 9, 10, false)]
    public void Target_selection_never_uses_lower_or_higher_version_fallback(
        ZLinkFrameworkRelocationMode mode,
        long exactTargetVersion,
        long candidateVersion,
        bool expected)
    {
        var selection = new ZLinkRelocationTargetSelection(
            mode,
            exactTargetVersion);

        Assert.Equal(expected, selection.Matches(candidateVersion));
    }

    [Fact]
    public async Task Relocate_before_serving_is_blocked_but_shutdown_is_allowed()
    {
        using var fixture = Create();
        fixture.Executor.Complete.TrySetResult(null);

        var blocked = await fixture.Runtime.RelocateAsync(new ZLinkFrameworkRelocationOptions
        {
            Mode = ZLinkFrameworkRelocationMode.PlannedMaintenance
        });
        var stopped = await fixture.Runtime.ShutdownAsync();

        Assert.Equal(ZLinkFrameworkRelocationReason.RuntimeNotReady, blocked.Reason);
        Assert.Equal(ZLinkFrameworkTerminationOutcome.Stopped, stopped.Outcome);
        Assert.Equal(ZLinkFrameworkRuntimeState.Stopped, fixture.Runtime.Status.State);
    }

    [Fact]
    public async Task Observe_reports_the_latest_relocated_status()
    {
        using var fixture = Create();
        fixture.Runtime.MarkServing();
        var observed = new List<ZLinkFrameworkRuntimeStatus>();
        using var stop = new CancellationTokenSource();
        var observer = Task.Run(async () =>
        {
            await foreach (var status in fixture.Runtime.ObserveAsync(stop.Token))
            {
                observed.Add(status);
                if (status.State == ZLinkFrameworkRuntimeState.Relocated)
                    break;
            }
        });

        fixture.Executor.Complete.TrySetResult(null);
        await fixture.Runtime.RelocateAsync(new ZLinkFrameworkRelocationOptions
        {
            Mode = ZLinkFrameworkRelocationMode.PlannedMaintenance
        });
        await observer.WaitAsync(TimeSpan.FromSeconds(2));

        Assert.Contains(observed, status =>
            status.State == ZLinkFrameworkRuntimeState.Relocated);
    }

    private static Fixture Create(
        Func<
            ZLinkFrameworkRelocationMode,
            long,
            CancellationToken,
            ValueTask<ZLinkFrameworkRelocationReason?>>? preflight = null,
        long sourceApplicationVersion = 0)
    {
        var executor = new MaintenanceExecutor();
        var drain = new ZLinkDrainCoordinator(
            new ZLinkDrainAdmissionGate(),
            executor,
            events: null);
        var runtime = new ZLinkFrameworkMaintenanceRuntime(
            drain,
            preflight ?? (static (_, _, _) =>
                ValueTask.FromResult<ZLinkFrameworkRelocationReason?>(null)),
            static _ => ValueTask.FromResult(true),
            sourceApplicationVersion);
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

        public int ExecuteCount { get; private set; }

        public ZLinkFrameworkLifecycleIntent? Intent { get; private set; }

        public ValueTask<ZLinkDrainForceReason?> ExecuteAsync(
            TimeSpan deadline,
            CancellationToken deadlineToken) =>
            ExecuteAsync(ZLinkFrameworkLifecycleIntent.Shutdown, deadline, deadlineToken);

        public async ValueTask<ZLinkDrainForceReason?> ExecuteAsync(
            ZLinkFrameworkLifecycleIntent intent,
            TimeSpan deadline,
            CancellationToken deadlineToken)
        {
            _ = deadline;
            Intent = intent;
            ExecuteCount++;
            return await Complete.Task.WaitAsync(deadlineToken).ConfigureAwait(false);
        }

        public ValueTask ForceStopAsync(
            ZLinkDrainForceReason reason,
            CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;
    }
}
