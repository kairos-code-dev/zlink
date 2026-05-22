using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotNodeCatalog(
    IServiceProvider services,
    ZLinkFrameworkRuntime runtime,
    ZLinkFrameworkRegistration frameworkRegistration,
    ZLinkSpotNodeRegistration registration,
    IZLinkBackendSpotNode node,
    string spotChannelName,
    Func<IZLinkBackendDiscovery?> discoveryProvider,
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
        connectDiscoveredPubSubPeers);

    public IReadOnlyCollection<ZLinkSpotActivation> Spots => SnapshotActivations();

    public async ValueTask<ZLinkSpotCreateResult> CreateAsync(
        string spotName,
        IReadOnlyList<Message> createParts,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(createParts);
        Type spotType;
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            spotType = ResolveSpotTypeLocked(spotName);
        }

        EnsureAttachedChannelBundles();

        var nativeSpot = node.CreateSpot();
        ZLinkSpotActivation? activation = null;
        try
        {
            activation = await _activationFactory.CreateAsync(
                spotName,
                spotType,
                nativeSpot,
                createParts,
                cancellationToken);

            cancellationToken.ThrowIfCancellationRequested();
            lock (_gate)
            {
                _spots.Add(activation.SpotRid, activation);
            }

            await BindSpotNameRouteAsync(
                activation.SpotName,
                activation.SpotRid,
                cancellationToken).ConfigureAwait(false);

            return new ZLinkSpotCreateResult(activation.SpotRid, spotName, true);
        }
        catch (Exception error)
        {
            RemoveActivation(activation);
            await DisposeFailedCreationAsync(nativeSpot, activation);

            throw WrapSpotCreateFailed(spotName, error);
        }
    }

    public async ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync(
        string spotName,
        RoutingId requestedSpotRid,
        IReadOnlyList<Message> createParts,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(createParts);
        Type spotType;
        PendingSpotCreation pending;
        var owner = false;
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            spotType = ResolveSpotTypeLocked(spotName);

            if (_spots.TryGetValue(requestedSpotRid, out var existing))
            {
                ThrowIfSpotNameMismatch(existing.SpotName, spotName, requestedSpotRid);
                return new ZLinkSpotCreateResult(existing.SpotRid, existing.SpotName, false);
            }

            if (_pending.TryGetValue(requestedSpotRid, out pending!))
            {
                ThrowIfSpotNameMismatch(pending.SpotName, spotName, requestedSpotRid);
            }
            else
            {
                pending = new PendingSpotCreation(spotName);
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
                spotName,
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

            await BindSpotNameRouteAsync(
                activation.SpotName,
                activation.SpotRid,
                cancellationToken).ConfigureAwait(false);

            lock (_gate)
            {
                pending.Complete(activation);
            }

            return new ZLinkSpotCreateResult(activation.SpotRid, spotName, true);
        }
        catch (Exception error)
        {
            var wrapped = WrapSpotCreateFailed(spotName, error);
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

    public async ValueTask<ZLinkSpotInfo?> GetAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            return _spots.TryGetValue(spotRid, out var activation)
                ? new ZLinkSpotInfo(activation.SpotRid, activation.SpotName)
                : null;
        }
    }

    public async ValueTask<IReadOnlyList<ZLinkSpotInfo>> ListAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            return _spots.Values
                .Select(static activation => new ZLinkSpotInfo(activation.SpotRid, activation.SpotName))
                .OrderBy(static item => item.SpotName, StringComparer.Ordinal)
                .ToArray();
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

        await UnbindSpotNameRouteAsync(
            activation.SpotName,
            cancellationToken).ConfigureAwait(false);
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

    private Type ResolveSpotTypeLocked(string spotName)
    {
        if (!registration.SpotFactories.TryGetValue(spotName, out var spotType))
        {
            throw new ZLinkConfigurationException(
                $"SPOT factory '{spotName}' is not registered on node '{registration.SpotNodeName}'.");
        }

        return spotType;
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
        return new ZLinkSpotCreateResult(activation.SpotRid, activation.SpotName, created);
    }

    private static void ThrowIfSpotNameMismatch(
        string existingSpotName,
        string requestedSpotName,
        RoutingId spotRid)
    {
        if (string.Equals(existingSpotName, requestedSpotName, StringComparison.Ordinal))
        {
            return;
        }

        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.SpotTypeMismatch,
            $"SPOT routing id '{spotRid}' already belongs to '{existingSpotName}'.");
    }

    private static ZLinkFrameworkException WrapSpotCreateFailed(
        string spotName,
        Exception error)
    {
        if (error is ZLinkFrameworkException frameworkError)
        {
            return frameworkError;
        }

        return new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.SpotCreateFailed,
            $"SPOT '{spotName}' creation failed.",
            innerException: error);
    }

    private async ValueTask BindSpotNameRouteAsync(
        string spotName,
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        var options = frameworkRegistration.RegistrySpotRemoteAddresses;
        if (options is null)
        {
            return;
        }

        var discovery = discoveryProvider()
            ?? throw new ZLinkConfigurationException(
                "Registry SPOT route publishing requires configured SPOT discovery.");
        var key = ZLinkRegistrySpotRemoteAddressResolver.BuildSpotNameRouteKey(
            options.Namespace,
            spotName);
        var value = ZLinkRegistrySpotRemoteAddressResolver.EncodeSpotNameRouteValue(
            options.Namespace,
            spotName,
            spotRid);
        await ZLinkRegistryRouteRuntime.RetryRouteOperationAsync(
                () => discovery.BindRoute(DiscoveryRouteKind.SpotName, key, value),
                $"SPOT name route publish failed for '{spotName}'.",
                ZLinkFrameworkErrorKind.SpotRouteNotFound,
                frameworkRegistration.DefaultTimeout,
                TimeSpan.FromMilliseconds(25),
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask UnbindSpotNameRouteAsync(
        string spotName,
        CancellationToken cancellationToken)
    {
        var options = frameworkRegistration.RegistrySpotRemoteAddresses;
        if (options is null)
        {
            return;
        }

        var discovery = discoveryProvider();
        if (discovery is null)
        {
            return;
        }

        var key = ZLinkRegistrySpotRemoteAddressResolver.BuildSpotNameRouteKey(
            options.Namespace,
            spotName);
        await ZLinkRegistryRouteRuntime.RetryRouteOperationAsync(
                () => discovery.UnbindRoute(DiscoveryRouteKind.SpotName, key),
                $"SPOT name route unbind failed for '{spotName}'.",
                ZLinkFrameworkErrorKind.SpotRouteNotFound,
                frameworkRegistration.DefaultTimeout,
                TimeSpan.FromMilliseconds(25),
                cancellationToken,
                ignoreNotFound: true)
            .ConfigureAwait(false);
    }

    private sealed class PendingSpotCreation(string spotName)
    {
        private readonly TaskCompletionSource<ZLinkSpotActivation> _completion =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public string SpotName { get; } = spotName;

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
