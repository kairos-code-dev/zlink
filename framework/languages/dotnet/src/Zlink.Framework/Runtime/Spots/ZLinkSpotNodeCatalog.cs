using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotNodeCatalog(
    IServiceProvider services,
    ZLinkFrameworkRuntime runtime,
    ZLinkFrameworkRegistration frameworkRegistration,
    ZLinkSpotNodeRegistration registration,
    IZLinkBackendSpotNode node,
    string spotChannelName,
    Func<string, ZLinkSpotAttachedChannelBundle> getOrCreateAttachedChannelBundle,
    Action connectDiscoveredPubSubPeers) : IAsyncDisposable
{
    private readonly object _gate = new();
    private readonly Dictionary<RoutingId, ZLinkSpotActivation> _spots = [];
    private readonly Dictionary<RoutingId, PendingSpotCreation> _pending = [];
    private readonly ZLinkSpotActivationFactory _activationFactory = new(
        services,
        runtime,
        frameworkRegistration,
        registration,
        node,
        spotChannelName,
        channelName => registration.AttachedChannelClients.ContainsKey(channelName)
            ? getOrCreateAttachedChannelBundle(channelName).Submitter
            : null,
        connectDiscoveredPubSubPeers);

    public IReadOnlyCollection<ZLinkSpotActivation> Spots => SnapshotActivations();

    public async ValueTask<ZLinkSpotCreateResult> CreateAsync(
        Type spotType,
        Message request,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            EnsureSpotTypeRegisteredLocked(spotType);
        }

        EnsureAttachedChannelBundles();

        var nativeSpot = node.CreateSpot();
        ZLinkSpotActivation? activation = null;
        try
        {
            var creation = await _activationFactory.CreateAsync(
                spotType,
                nativeSpot,
                request,
                cancellationToken);
            activation = creation.Activation;

            if (!creation.Response.Accepted)
            {
                var rejected = new ZLinkSpotCreateResult(
                    activation.SpotRid,
                    ZLinkSpotCreateState.Rejected,
                    creation.Response.Reply);
                await DisposeFailedCreationAsync(nativeSpot, activation);
                return rejected;
            }

            cancellationToken.ThrowIfCancellationRequested();
            lock (_gate)
            {
                _spots.Add(activation.SpotRid, activation);
            }

            return new ZLinkSpotCreateResult(
                activation.SpotRid,
                ZLinkSpotCreateState.Created,
                creation.Response.Reply);
        }
        catch (Exception error)
        {
            RemoveActivation(activation);
            await DisposeFailedCreationAsync(nativeSpot, activation);

            throw WrapSpotCreateFailed(spotType, error);
        }
    }

    public async ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync(
        Type spotType,
        RoutingId requestedSpotRid,
        Message request,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);
        PendingSpotCreation pending;
        var owner = false;
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            EnsureSpotTypeRegisteredLocked(spotType);

            if (_spots.TryGetValue(requestedSpotRid, out var existing))
            {
                ThrowIfSpotTypeMismatch(existing.Spot.GetType(), spotType, requestedSpotRid);
                return new ZLinkSpotCreateResult(
                    existing.SpotRid,
                    ZLinkSpotCreateState.Existing,
                    null);
            }

            if (_pending.TryGetValue(requestedSpotRid, out pending!))
            {
                ThrowIfSpotTypeMismatch(pending.SpotType, spotType, requestedSpotRid);
            }
            else
            {
                pending = new PendingSpotCreation(spotType);
                _pending.Add(requestedSpotRid, pending);
                owner = true;
            }
        }

        if (!owner)
        {
            var result = await pending.Task.ConfigureAwait(false);
            return result.State == ZLinkSpotCreateState.Created
                ? result with { State = ZLinkSpotCreateState.Existing }
                : result;
        }

        EnsureAttachedChannelBundles();

        IZLinkBackendSpot? nativeSpot = null;
        ZLinkSpotActivation? activation = null;
        try
        {
            nativeSpot = node.GetOrCreateSpot(requestedSpotRid, out var created);
            if (!created)
            {
                await nativeSpot.DisposeAsync();
                nativeSpot = null;
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.SpotCreateFailed,
                    $"SPOT routing id '{requestedSpotRid}' already exists in core but no framework SPOT is registered.");
            }

            var creation = await _activationFactory.CreateAsync(
                spotType,
                nativeSpot,
                request,
                cancellationToken);
            activation = creation.Activation;

            if (!creation.Response.Accepted)
            {
                var rejected = new ZLinkSpotCreateResult(
                    activation.SpotRid,
                    ZLinkSpotCreateState.Rejected,
                    creation.Response.Reply);
                lock (_gate)
                {
                    _pending.Remove(requestedSpotRid);
                    pending.Complete(rejected);
                }

                await DisposeFailedCreationAsync(nativeSpot, activation);
                return rejected;
            }

            cancellationToken.ThrowIfCancellationRequested();
            lock (_gate)
            {
                _pending.Remove(requestedSpotRid);
                _spots.Add(activation.SpotRid, activation);
            }

            var result = new ZLinkSpotCreateResult(
                activation.SpotRid,
                ZLinkSpotCreateState.Created,
                creation.Response.Reply);
            lock (_gate)
            {
                pending.Complete(result);
            }

            return result;
        }
        catch (Exception error)
        {
            var wrapped = WrapSpotCreateFailed(spotType, error);
            lock (_gate)
            {
                _pending.Remove(requestedSpotRid);
                RemoveActivationLocked(activation);

                pending.Fail(wrapped);
            }

            await DisposeFailedCreationAsync(nativeSpot, activation);

            throw wrapped;
        }
    }

    public ValueTask<ZLinkSpotInfo?> GetAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            ZLinkSpotInfo? result = _spots.TryGetValue(spotRid, out var activation)
                ? new ZLinkSpotInfo(activation.SpotRid)
                : null;
            return ValueTask.FromResult(result);
        }
    }

    public ValueTask<IReadOnlyList<ZLinkSpotInfo>> ListAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            IReadOnlyList<ZLinkSpotInfo> result = _spots.Values
                .Select(static activation => new ZLinkSpotInfo(activation.SpotRid))
                .OrderBy(static item => item.SpotRid.ToHex(), StringComparer.Ordinal)
                .ToArray();
            return ValueTask.FromResult(result);
        }
    }

    public async ValueTask<bool> CloseAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        ZLinkSpotActivation? activation;
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            if (!_spots.TryGetValue(spotRid, out activation))
            {
                return false;
            }

            if (activation.JoinedActorCount > 0)
            {
                return false;
            }

            _spots.Remove(spotRid);
        }

        if (ReferenceEquals(ZLinkSpotAmbientContext.CurrentOrDefault, activation))
        {
            _ = CloseActivationAfterCurrentTurnAsync(activation).ContinueWith(
                static task => _ = task.Exception,
                CancellationToken.None,
                TaskContinuationOptions.OnlyOnFaulted | TaskContinuationOptions.ExecuteSynchronously,
                TaskScheduler.Default);
            return true;
        }

        await activation.CloseAsync(cancellationToken);
        await activation.DisposeAsync();
        return true;
    }

    public async ValueTask DisposeAsync()
    {
        ZLinkSpotActivation[] activations;
        lock (_gate)
        {
            activations = _spots.Values.ToArray();
            _spots.Clear();
        }

        foreach (var activation in activations)
        {
            await activation.DisposeAsync();
        }
    }

    private IReadOnlyCollection<ZLinkSpotActivation> SnapshotActivations()
    {
        lock (_gate)
        {
            return _spots.Values.ToArray();
        }
    }

    private static async Task CloseActivationAfterCurrentTurnAsync(ZLinkSpotActivation activation)
    {
        try
        {
            await activation.CloseAsync(CancellationToken.None).ConfigureAwait(false);
        }
        finally
        {
            await activation.DisposeAsync().ConfigureAwait(false);
        }
    }

    private void RemoveActivation(ZLinkSpotActivation? activation)
    {
        if (activation is null)
        {
            return;
        }

        lock (_gate)
        {
            RemoveActivationLocked(activation);
        }
    }

    private void RemoveActivationLocked(ZLinkSpotActivation? activation)
    {
        if (activation is not null)
        {
            _spots.Remove(activation.SpotRid);
        }
    }

    private static async ValueTask DisposeFailedCreationAsync(
        IZLinkBackendSpot? nativeSpot,
        ZLinkSpotActivation? activation)
    {
        if (activation is not null)
        {
            await activation.DisposeAsync();
            return;
        }

        if (nativeSpot is not null)
        {
            await nativeSpot.DisposeAsync();
        }
    }

    private void EnsureSpotTypeRegisteredLocked(Type spotType)
    {
        if (!registration.SpotFactories.Contains(spotType))
        {
            throw new ZLinkConfigurationException(
                $"SPOT factory '{spotType}' is not registered on node '{registration.SpotNodeName}'.");
        }
    }

    private void EnsureAttachedChannelBundles()
    {
        foreach (var channelName in registration.AttachedChannelClients.Keys)
        {
            getOrCreateAttachedChannelBundle(channelName);
        }
    }

    private static void ThrowIfSpotTypeMismatch(
        Type existingSpotType,
        Type requestedSpotType,
        RoutingId spotRid)
    {
        if (existingSpotType == requestedSpotType)
        {
            return;
        }

        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.SpotTypeMismatch,
            $"SPOT routing id '{spotRid}' already belongs to '{existingSpotType}'.");
    }

    private static ZLinkFrameworkException WrapSpotCreateFailed(
        Type spotType,
        Exception error)
    {
        if (error is ZLinkFrameworkException frameworkError)
        {
            return frameworkError;
        }

        return new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.SpotCreateFailed,
            $"SPOT '{spotType}' creation failed.",
            innerException: error);
    }

    private sealed class PendingSpotCreation(Type spotType)
    {
        private readonly TaskCompletionSource<ZLinkSpotCreateResult> _completion =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public Type SpotType { get; } = spotType;

        public Task<ZLinkSpotCreateResult> Task => _completion.Task;

        public void Complete(ZLinkSpotCreateResult result)
        {
            _completion.TrySetResult(result);
        }

        public void Fail(Exception error)
        {
            _completion.TrySetException(error);
        }
    }
}
