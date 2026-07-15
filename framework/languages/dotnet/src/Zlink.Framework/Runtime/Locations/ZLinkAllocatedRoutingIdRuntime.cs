using System.Globalization;

namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Owns routing-id allocation as one runtime lifecycle. It acquires every group before any
/// framework socket is created, applies all member ids together, and releases only after the
/// sockets have stopped.
/// </summary>
internal sealed class ZLinkAllocatedRoutingIdRuntime : IZLinkAllocatedRoutingIdProvider
{
    private readonly IZLinkRoutingIdSlotAllocationStore _store;
    private readonly ZLinkLocationRuntime _locations;
    private readonly ZLinkLocationOptions _options;
    private readonly TimeProvider _time;
    private readonly IReadOnlyList<AllocationGroup> _groups;
    private readonly Dictionary<string, TaskCompletionSource<ZLinkAllocatedRoutingId>> _ready;
    private readonly SemaphoreSlim _lifecycle = new(1, 1);
    private readonly object _fenceGate = new();
    private IReadOnlyList<AcquiredGroup> _acquired = [];
    private CancellationTokenSource? _fenceStop;
    private Task? _fenceMonitor;
    private long _fenceDeadline;
    private bool _leaseAtRisk;
    private bool _started;
    private int _fenced;

    internal ZLinkAllocatedRoutingIdRuntime(
        ZLinkFrameworkRegistration registration,
        IZLinkRoutingIdSlotAllocationStore store,
        ZLinkLocationRuntime locations,
        ZLinkLocationOptions options,
        TimeProvider? timeProvider = null)
    {
        _store = store;
        _locations = locations;
        _options = options;
        _time = timeProvider ?? TimeProvider.System;
        _groups = BuildGroups(registration);
        _ready = _groups.ToDictionary(
            static group => group.Name,
            static _ => NewReadySource(),
            StringComparer.Ordinal);
    }

    internal event Action? FencingRequired;

    internal bool Enabled => _groups.Count != 0;

    internal async ValueTask StartAsync(CancellationToken cancellationToken)
    {
        if (!Enabled) return;

        await _lifecycle.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            if (_started) return;
            _started = true;
            Volatile.Write(ref _fenced, 0);
            ResetReadySources();

            while (true)
            {
                var acquired = new List<AcquiredGroup>(_groups.Count);
                var exhausted = false;
                var storeUnavailable = false;
                try
                {
                    foreach (var group in _groups)
                    {
                        ZLinkRoutingIdSlotAcquireResult result;
                        try
                        {
                            result = await _store.AcquireRoutingIdSlotAsync(
                                    new ZLinkRoutingIdSlotAcquireRequest(
                                        group.Name,
                                        group.StoreMembers,
                                        group.SlotCount,
                                        _locations.OwnerId,
                                        _options.OwnerLeaseTtl),
                                    cancellationToken)
                                .ConfigureAwait(false);
                        }
                        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
                        {
                            throw;
                        }
                        catch
                        {
                            storeUnavailable = true;
                            break;
                        }

                        switch (result)
                        {
                            case ZLinkRoutingIdSlotAcquired success:
                                acquired.Add(new AcquiredGroup(group, success.Allocation));
                                ConfirmLease(success.Allocation.LeaseExpiresAt, success.Allocation.StoreNow);
                                break;
                            case ZLinkRoutingIdSlotGroupExhausted:
                                exhausted = true;
                                break;
                            case ZLinkRoutingIdSlotGroupConfigurationMismatch mismatch:
                                throw new ZLinkConfigurationException(
                                    $"Routing-id allocation group '{group.Name}' does not match its stored member configuration "
                                    + $"(expected count {mismatch.ExpectedSlotCount}, actual count {mismatch.ActualSlotCount}).");
                            case ZLinkRoutingIdSlotIdentityModeConflict:
                                throw new ZLinkConfigurationException(
                                    $"Routing-id allocation group '{group.Name}' conflicts with fixed routing-id peers.");
                            default:
                                throw new InvalidOperationException(
                                    $"Unknown routing-id allocation result '{result.GetType().Name}'.");
                        }

                        if (exhausted) break;
                    }

                    if (!exhausted && !storeUnavailable)
                    {
                        Apply(acquired);
                        _acquired = acquired;
                        StartFenceMonitor();
                        return;
                    }
                }
                catch
                {
                    await ReleaseAsync(acquired, CancellationToken.None).ConfigureAwait(false);
                    throw;
                }

                await TryReleaseForRetryAsync(acquired).ConfigureAwait(false);
                await Task.Delay(_options.PollingInterval, _time, cancellationToken).ConfigureAwait(false);
            }
        }
        catch
        {
            _started = false;
            throw;
        }
        finally
        {
            _lifecycle.Release();
        }
    }

    internal void MarkReady()
    {
        if (!Enabled) return;
        foreach (var acquired in _acquired)
        {
            var memberIds = acquired.Group.Members.ToDictionary(
                static member => member.MemberName,
                member => RoutingId.From(member.Prefix + acquired.Allocation.Slot.ToString(CultureInfo.InvariantCulture)),
                StringComparer.Ordinal);
            _ready[acquired.Group.Name].TrySetResult(new ZLinkAllocatedRoutingId(
                acquired.Group.Name,
                acquired.Allocation.Slot,
                memberIds));
        }
    }

    internal async ValueTask StopAsync(CancellationToken cancellationToken)
    {
        if (!Enabled) return;
        await _lifecycle.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            if (!_started) return;
            _started = false;
            await StopFenceMonitorAsync().ConfigureAwait(false);
            var acquired = _acquired;
            _acquired = [];
            await ReleaseAsync(acquired, cancellationToken).ConfigureAwait(false);
        }
        finally
        {
            _lifecycle.Release();
        }
    }

    public async ValueTask<ZLinkAllocatedRoutingId> WaitForReadyAllocationAsync(
        string groupName,
        CancellationToken cancellationToken = default)
    {
        if (!_ready.TryGetValue(groupName, out var source))
            throw new ZLinkConfigurationException(
                $"Routing-id allocation group '{groupName}' is not registered.");
        return await source.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
    }

    private void Apply(IReadOnlyList<AcquiredGroup> acquired)
    {
        foreach (var item in acquired)
            foreach (var member in item.Group.Members)
                member.Apply(RoutingId.From(
                    member.Prefix + item.Allocation.Slot.ToString(CultureInfo.InvariantCulture)));
    }

    private async ValueTask ReleaseAsync(
        IReadOnlyList<AcquiredGroup> acquired,
        CancellationToken cancellationToken)
    {
        List<Exception>? failures = null;
        for (var index = acquired.Count - 1; index >= 0; index--)
        {
            var item = acquired[index];
            try
            {
                await _store.ReleaseRoutingIdSlotAsync(
                        item.Group.Name,
                        item.Allocation.Slot,
                        item.Allocation.Owner,
                        cancellationToken)
                    .ConfigureAwait(false);
            }
            catch (Exception error)
            {
                (failures ??= []).Add(error);
            }
        }

        if (failures is { Count: 1 }) throw failures[0];
        if (failures is { Count: > 1 }) throw new AggregateException(failures);
    }

    private async ValueTask TryReleaseForRetryAsync(IReadOnlyList<AcquiredGroup> acquired)
    {
        try
        {
            await ReleaseAsync(acquired, CancellationToken.None).ConfigureAwait(false);
        }
        catch
        {
            // The same owner retries idempotently. If the store is still unavailable, the shared
            // owner lease provides the final cleanup boundary for any claim that could not be
            // released during this attempt.
        }
    }

    private void StartFenceMonitor()
    {
        _locations.OwnerLeaseRenewed += OnOwnerLeaseRenewed;
        _locations.OwnerLeaseRenewalFailed += OnOwnerLeaseRenewalFailed;
        var stop = new CancellationTokenSource();
        _fenceStop = stop;
        _fenceMonitor = MonitorFenceDeadlineAsync(stop.Token);
    }

    private async ValueTask StopFenceMonitorAsync()
    {
        _locations.OwnerLeaseRenewed -= OnOwnerLeaseRenewed;
        _locations.OwnerLeaseRenewalFailed -= OnOwnerLeaseRenewalFailed;
        var stop = _fenceStop;
        var monitor = _fenceMonitor;
        _fenceStop = null;
        _fenceMonitor = null;
        if (stop is null) return;
        await stop.CancelAsync().ConfigureAwait(false);
        if (monitor is not null)
            try
            {
                await monitor.ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (stop.IsCancellationRequested)
            {
            }
        stop.Dispose();
    }

    private void OnOwnerLeaseRenewed(ZLinkOwnerLeaseRenewal renewal)
    {
        ConfirmLease(renewal.LeaseExpiresAt, renewal.StoreNow);
        lock (_fenceGate) _leaseAtRisk = false;
    }

    private void OnOwnerLeaseRenewalFailed()
    {
        lock (_fenceGate) _leaseAtRisk = true;
    }

    private void ConfirmLease(DateTimeOffset expiresAt, DateTimeOffset storeNow)
    {
        var safeFor = expiresAt - storeNow - _options.RoutingIdFencingMargin;
        if (safeFor <= TimeSpan.Zero)
            throw new ZLinkConfigurationException(
                "The allocated routing-id owner lease has no positive fencing interval.");
        var deadline = _time.GetTimestamp()
                       + (long)(safeFor.TotalSeconds * _time.TimestampFrequency);
        lock (_fenceGate)
        {
            _fenceDeadline = deadline;
            _leaseAtRisk = false;
        }
    }

    private async Task MonitorFenceDeadlineAsync(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            long deadline;
            bool atRisk;
            lock (_fenceGate)
            {
                deadline = _fenceDeadline;
                atRisk = _leaseAtRisk;
            }

            if (atRisk && _time.GetTimestamp() >= deadline)
            {
                if (Interlocked.Exchange(ref _fenced, 1) == 0)
                {
                    var error = new InvalidOperationException(
                        "The allocated routing-id owner lease could not be renewed before its fencing deadline.");
                    foreach (var source in _ready.Values) source.TrySetException(error);
                    FencingRequired?.Invoke();
                }
                return;
            }

            await Task.Delay(
                    atRisk ? TimeSpan.FromMilliseconds(25) : _options.HeartbeatInterval,
                    _time,
                    cancellationToken)
                .ConfigureAwait(false);
        }
    }

    private void ResetReadySources()
    {
        foreach (var group in _groups)
            if (_ready[group.Name].Task.IsCompleted)
                _ready[group.Name] = NewReadySource();
    }

    private static TaskCompletionSource<ZLinkAllocatedRoutingId> NewReadySource() =>
        new(TaskCreationOptions.RunContinuationsAsynchronously);

    private static IReadOnlyList<AllocationGroup> BuildGroups(ZLinkFrameworkRegistration registration) =>
        ZLinkRoutingIdAllocationCatalog.Collect(registration)
            .GroupBy(static member => member.GroupName, StringComparer.Ordinal)
            .OrderBy(static group => group.Key, StringComparer.Ordinal)
            .Select(group =>
            {
                var members = group.OrderBy(static member => member.MemberName, StringComparer.Ordinal).ToArray();
                return new AllocationGroup(
                    group.Key,
                    members[0].SlotCount,
                    members,
                    members.Select(static member => new ZLinkRoutingIdSlotAllocationMember(
                        member.MemberName,
                        member.Prefix)).ToArray());
            })
            .ToArray();

    private sealed record AllocationGroup(
        string Name,
        int SlotCount,
        IReadOnlyList<ZLinkRoutingIdAllocationMemberRegistration> Members,
        IReadOnlyList<ZLinkRoutingIdSlotAllocationMember> StoreMembers);

    private sealed record AcquiredGroup(
        AllocationGroup Group,
        ZLinkRoutingIdSlotAllocation Allocation);
}
