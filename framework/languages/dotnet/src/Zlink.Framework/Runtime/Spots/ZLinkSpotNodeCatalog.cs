using Microsoft.Extensions.DependencyInjection;
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
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly Dictionary<RoutingId, ZLinkSpotActivation> _spots = [];
    private readonly Dictionary<RoutingId, PendingSpotCreation> _pending = [];

    public IReadOnlyCollection<ZLinkSpotActivation> Spots => SnapshotActivations();

    public async ValueTask<ZLinkSpotCreateResult> CreateAsync(
        string spotName,
        IReadOnlyList<Message> createParts,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(createParts);
        Type spotType;
        await _gate.WaitAsync(cancellationToken);
        try
        {
            spotType = PrepareCreationLocked(spotName);
        }
        finally
        {
            _gate.Release();
        }

        var nativeSpot = node.CreateSpot();
        ZLinkSpotActivation? activation = null;
        try
        {
            activation = await CreateActivationAsync(
                spotName,
                spotType,
                nativeSpot,
                createParts,
                cancellationToken);

            await _gate.WaitAsync(cancellationToken);
            try
            {
                _spots.Add(activation.SpotRid, activation);
            }
            finally
            {
                _gate.Release();
            }

            return new ZLinkSpotCreateResult(activation.SpotRid, spotName, true);
        }
        catch (Exception error)
        {
            if (activation is null)
            {
                await nativeSpot.DisposeAsync();
            }
            else
            {
                await activation.DisposeAsync();
            }

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
        await _gate.WaitAsync(cancellationToken);
        try
        {
            spotType = PrepareCreationLocked(spotName);

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
        finally
        {
            _gate.Release();
        }

        if (!owner)
        {
            return await AwaitPendingAsync(pending, created: false);
        }

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

            activation = await CreateActivationAsync(
                spotName,
                spotType,
                nativeSpot,
                createParts,
                cancellationToken);

            await _gate.WaitAsync(cancellationToken);
            try
            {
                _pending.Remove(requestedSpotRid);
                _spots.Add(activation.SpotRid, activation);
                pending.Complete(activation);
            }
            finally
            {
                _gate.Release();
            }

            return new ZLinkSpotCreateResult(activation.SpotRid, spotName, true);
        }
        catch (Exception error)
        {
            var wrapped = WrapSpotCreateFailed(spotName, error);
            await _gate.WaitAsync(CancellationToken.None);
            try
            {
                _pending.Remove(requestedSpotRid);
                pending.Fail(wrapped);
            }
            finally
            {
                _gate.Release();
            }

            if (activation is null && nativeSpot is not null)
            {
                await nativeSpot.DisposeAsync();
            }
            else if (activation is not null)
            {
                await activation.DisposeAsync();
            }

            throw wrapped;
        }
    }

    public async ValueTask<ZLinkSpotInfo?> GetAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        await _gate.WaitAsync(cancellationToken);
        try
        {
            return _spots.TryGetValue(spotRid, out var activation)
                ? new ZLinkSpotInfo(activation.SpotRid, activation.SpotName)
                : null;
        }
        finally
        {
            _gate.Release();
        }
    }

    public async ValueTask<IReadOnlyList<ZLinkSpotInfo>> ListAsync(CancellationToken cancellationToken)
    {
        await _gate.WaitAsync(cancellationToken);
        try
        {
            return _spots.Values
                .Select(static activation => new ZLinkSpotInfo(activation.SpotRid, activation.SpotName))
                .OrderBy(static item => item.SpotName, StringComparer.Ordinal)
                .ToArray();
        }
        finally
        {
            _gate.Release();
        }
    }

    public async ValueTask<bool> RemoveAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        ZLinkSpotActivation? activation;
        await _gate.WaitAsync(cancellationToken);
        try
        {
            if (!_spots.Remove(spotRid, out activation))
            {
                return false;
            }
        }
        finally
        {
            _gate.Release();
        }

        await activation.CloseAsync(cancellationToken);
        await activation.DisposeAsync();
        return true;
    }

    public async ValueTask DisposeAsync()
    {
        ZLinkSpotActivation[] activations;
        _gate.Wait();
        try
        {
            activations = _spots.Values.ToArray();
            _spots.Clear();
        }
        finally
        {
            _gate.Release();
        }

        foreach (var activation in activations)
        {
            await activation.DisposeAsync();
        }

        _gate.Dispose();
    }

    private IReadOnlyCollection<ZLinkSpotActivation> SnapshotActivations()
    {
        _gate.Wait();
        try
        {
            return _spots.Values.ToArray();
        }
        finally
        {
            _gate.Release();
        }
    }

    private Type PrepareCreationLocked(string spotName)
    {
        if (!registration.SpotFactories.TryGetValue(spotName, out var spotType))
        {
            throw new ZLinkConfigurationException(
                $"SPOT factory '{spotName}' is not registered on node '{registration.SpotNodeName}'.");
        }

        foreach (var channelName in registration.AttachedChannelClients.Keys)
        {
            getOrCreateAttachedChannelBundle(channelName);
        }

        return spotType;
    }

    private async ValueTask<ZLinkSpotActivation> CreateActivationAsync(
        string spotName,
        Type spotType,
        IZLinkBackendSpot nativeSpot,
        IReadOnlyList<Message> createParts,
        CancellationToken cancellationToken)
    {
        connectDiscoveredPubSubPeers();
        var spotScope = services.CreateAsyncScope();
        ZLinkSpotActivation? activation = null;
        try
        {
            activation = new ZLinkSpotActivation(
                runtime,
                spotScope,
                nativeSpot,
                node.RoutingId,
                spotName,
                spotChannelName,
                frameworkRegistration.DefaultTimeout,
                registration.Router?.SocketConfig.SendTimeout
                    ?? TimeSpan.FromMilliseconds(200));

            var spot = (IZLinkSpot)ActivatorUtilities.CreateInstance(
                spotScope.ServiceProvider,
                spotType,
                activation);

            activation.AttachSpot(spot);
            spot.Configure();
            activation.BindDescriptors();
            await activation.InitializeAsync(createParts, cancellationToken);
            return activation;
        }
        catch
        {
            if (activation is null)
            {
                await spotScope.DisposeAsync();
            }
            else
            {
                await activation.DisposeAsync();
            }

            throw;
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
