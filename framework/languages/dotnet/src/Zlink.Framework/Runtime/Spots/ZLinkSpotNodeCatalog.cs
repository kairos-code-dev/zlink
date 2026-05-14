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

    public IReadOnlyCollection<ZLinkSpotActivation> Spots => _spots.Values;

    public async ValueTask<ZLinkSpotCreateResult> CreateAsync(
        string spotName,
        RoutingId? requestedSpotRid,
        CancellationToken cancellationToken)
    {
        await _gate.WaitAsync(cancellationToken);
        try
        {
            if (!registration.SpotFactories.TryGetValue(spotName, out var spotType))
            {
                throw new InvalidOperationException(
                    $"SPOT factory '{spotName}' is not registered on node '{registration.SpotNodeName}'.");
            }

            foreach (var channelName in registration.AttachedChannelClients.Keys)
            {
                getOrCreateAttachedChannelBundle(channelName);
            }

            if (requestedSpotRid is RoutingId existingRid
                && _spots.TryGetValue(existingRid, out var existing))
            {
                if (!string.Equals(existing.SpotName, spotName, StringComparison.Ordinal))
                {
                    throw new InvalidOperationException(
                        $"SPOT routing id '{existingRid}' already belongs to '{existing.SpotName}'.");
                }

                return new ZLinkSpotCreateResult(existing.SpotRid, existing.SpotName, false);
            }

            var nativeSpot = node.CreateSpot();
            var spotCreated = false;
            ZLinkSpotActivation? activation = null;
            try
            {
                connectDiscoveredPubSubPeers();

                if (requestedSpotRid is RoutingId providedRid)
                {
                    nativeSpot.SetRoutingId(providedRid);
                }

                var spotScope = services.CreateAsyncScope();
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
                    await activation.InitializeAsync(cancellationToken);
                    _spots.Add(activation.SpotRid, activation);
                    spotCreated = true;

                    return new ZLinkSpotCreateResult(activation.SpotRid, spotName, true);
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
            finally
            {
                if (!spotCreated && activation is null)
                {
                    await nativeSpot.DisposeAsync();
                }
            }
        }
        finally
        {
            _gate.Release();
        }
    }

    public ValueTask<ZLinkSpotInfo?> GetAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();

        if (_spots.TryGetValue(spotRid, out var activation))
        {
            return ValueTask.FromResult<ZLinkSpotInfo?>(
                new ZLinkSpotInfo(activation.SpotRid, activation.SpotName));
        }

        return ValueTask.FromResult<ZLinkSpotInfo?>(null);
    }

    public ValueTask<IReadOnlyList<ZLinkSpotInfo>> ListAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        IReadOnlyList<ZLinkSpotInfo> items = _spots.Values
            .Select(static activation => new ZLinkSpotInfo(activation.SpotRid, activation.SpotName))
            .OrderBy(static item => item.SpotName, StringComparer.Ordinal)
            .ToArray();
        return ValueTask.FromResult(items);
    }

    public async ValueTask<bool> RemoveAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        await _gate.WaitAsync(cancellationToken);
        try
        {
            if (!_spots.Remove(spotRid, out var activation))
            {
                return false;
            }

            await activation.CloseAsync(cancellationToken);
            await activation.DisposeAsync();
            return true;
        }
        finally
        {
            _gate.Release();
        }
    }

    public async ValueTask DisposeAsync()
    {
        foreach (var activation in _spots.Values.ToArray())
        {
            await activation.DisposeAsync();
        }

        _gate.Dispose();
    }
}
