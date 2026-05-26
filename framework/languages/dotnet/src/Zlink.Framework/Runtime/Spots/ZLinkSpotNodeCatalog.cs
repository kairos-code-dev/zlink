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
        IReadOnlyList<Message> createParts,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(createParts);
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
            activation = await _activationFactory.CreateAsync(
                spotType,
                nativeSpot,
                createParts,
                cancellationToken);

            cancellationToken.ThrowIfCancellationRequested();
            lock (_gate)
            {
                _spots.Add(activation.SpotRid, activation);
            }

            return new ZLinkSpotCreateResult(activation.SpotRid, true);
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
        IReadOnlyList<Message> createParts,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(createParts);
        PendingSpotCreation pending;
        var owner = false;
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            EnsureSpotTypeRegisteredLocked(spotType);

            if (_spots.TryGetValue(requestedSpotRid, out var existing))
            {
                ThrowIfSpotTypeMismatch(existing.Spot.GetType(), spotType, requestedSpotRid);
                return new ZLinkSpotCreateResult(existing.SpotRid, false);
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
            return await AwaitPendingAsync(pending, created: false);
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

            activation = await _activationFactory.CreateAsync(
                spotType,
                nativeSpot,
                createParts,
                cancellationToken);

            cancellationToken.ThrowIfCancellationRequested();
            lock (_gate)
            {
                _pending.Remove(requestedSpotRid);
                _spots.Add(activation.SpotRid, activation);
            }

            lock (_gate)
            {
                pending.Complete(activation);
            }

            return new ZLinkSpotCreateResult(activation.SpotRid, true);
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

    public async ValueTask<bool> RemoveAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        ZLinkSpotActivation? activation;
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            if (!_spots.Remove(spotRid, out activation))
            {
                return false;
            }
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

    private static async ValueTask<ZLinkSpotCreateResult> AwaitPendingAsync(
        PendingSpotCreation pending,
        bool created)
    {
        var activation = await pending.Task.ConfigureAwait(false);
        return new ZLinkSpotCreateResult(activation.SpotRid, created);
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
        private readonly TaskCompletionSource<ZLinkSpotActivation> _completion =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public Type SpotType { get; } = spotType;

        public Task<ZLinkSpotActivation> Task => _completion.Task;

        public void Complete(ZLinkSpotActivation activation)
        {
            _completion.TrySetResult(activation);
        }

        public void Fail(Exception error)
        {
            _completion.TrySetException(error);
        }
    }
}
