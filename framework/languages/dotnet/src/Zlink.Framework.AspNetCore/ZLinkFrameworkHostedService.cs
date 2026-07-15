using Microsoft.Extensions.Hosting;
using Systems.Zlink;
using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.AspNetCore;

internal sealed class ZLinkFrameworkHostedService(
    ZLinkFrameworkRuntime runtime,
    ZLinkMonitoringRegistration? monitoringRegistration,
    ZLinkLocationRuntime? locationRuntime,
    ZLinkLocationAutoConnectHost? autoConnect,
    ZLinkLocationLifecycle? locationLifecycle,
    ZLinkAllocatedRoutingIdRuntime? allocatedRoutingIds,
    IHostApplicationLifetime? applicationLifetime,
    ZLinkDrainCoordinator drain) : IHostedService
{
    private readonly RoutingId _locationNodeRid = RoutingId.From(Guid.NewGuid().ToString("n"));
    private readonly object _fencingGate = new();
    private Task? _fencingTask;

    public async Task StartAsync(CancellationToken cancellationToken)
    {
        if (monitoringRegistration is not null)
            new ZLinkMonitoringSourceValidator(monitoringRegistration).PreflightFrameworkSources(runtime);

        try
        {
            if (locationRuntime is not null)
                await locationRuntime.StartAsync(
                        _locationNodeRid,
                        cancellationToken)
                    .ConfigureAwait(false);

            if (allocatedRoutingIds is not null)
            {
                if (applicationLifetime is not null)
                    allocatedRoutingIds.FencingRequired += OnFencingRequired;
                await allocatedRoutingIds.StartAsync(cancellationToken).ConfigureAwait(false);
            }

            await runtime.StartAsync(cancellationToken).ConfigureAwait(false);
            allocatedRoutingIds?.MarkReady();
            if (autoConnect is not null)
            {
                var state = await runtime.EnsureStartedStateAsync(cancellationToken).ConfigureAwait(false);
                await autoConnect.StartAsync(state, cancellationToken).ConfigureAwait(false);
            }
        }
        catch (Exception startFailure)
        {
            try
            {
                await StopCoreAsync().ConfigureAwait(false);
            }
            catch (Exception cleanupFailure)
            {
                throw new AggregateException(startFailure, cleanupFailure);
            }

            System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(startFailure).Throw();
        }
    }

    public Task StopAsync(CancellationToken cancellationToken)
    {
        return DrainAndStopAsync(cancellationToken);
    }

    private async Task DrainAndStopAsync(CancellationToken cancellationToken)
    {
        try
        {
            await drain.DrainAsync(cancellationToken).ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            await drain.AwaitDrainedAsync(CancellationToken.None).ConfigureAwait(false);
        }
        await StopCoreAsync().ConfigureAwait(false);
    }

    private async Task StopCoreAsync()
    {
        List<Exception>? failures = null;
        await TryStopAsync(
            () => autoConnect?.StopAsync(CancellationToken.None).AsTask() ?? Task.CompletedTask).ConfigureAwait(false);
        await TryStopAsync(
            () => runtime.StopAsync(CancellationToken.None).AsTask()).ConfigureAwait(false);
        await TryStopAsync(
            () => locationRuntime?.RemoveOwnedRowsBeforeRoutingIdReleaseAsync(CancellationToken.None).AsTask()
                  ?? Task.CompletedTask).ConfigureAwait(false);
        await TryStopAsync(
            () => allocatedRoutingIds?.StopAsync(CancellationToken.None).AsTask() ?? Task.CompletedTask)
            .ConfigureAwait(false);
        await TryStopAsync(
            () => locationRuntime?.StopAsync(CancellationToken.None).AsTask() ?? Task.CompletedTask).ConfigureAwait(false);
        if (allocatedRoutingIds is not null && applicationLifetime is not null)
            allocatedRoutingIds.FencingRequired -= OnFencingRequired;
        locationLifecycle?.ResetGeneration();

        if (failures is { Count: 1 }) throw failures[0];
        if (failures is { Count: > 1 }) throw new AggregateException(failures);
        return;

        async Task TryStopAsync(Func<Task> stop)
        {
            try
            {
                await stop().ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                (failures ??= []).Add(exception);
            }
        }
    }

    private void OnFencingRequired()
    {
        lock (_fencingGate)
            _fencingTask ??= FenceRuntimeAndStopHostAsync();
    }

    private async Task FenceRuntimeAndStopHostAsync()
    {
        try
        {
            // Self-fencing must not wait for the normal graceful-drain deadline. Closing the
            // framework runtime first removes every socket that uses the allocated identity;
            // the host then performs the remaining idempotent cleanup.
            await runtime.StopAsync(CancellationToken.None).ConfigureAwait(false);
        }
        finally
        {
            applicationLifetime?.StopApplication();
        }
    }
}
