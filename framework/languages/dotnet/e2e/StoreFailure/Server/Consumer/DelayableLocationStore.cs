using Systems.Zlink;
using Zlink.Framework.Contracts.Locations;

namespace StoreFailure.Server.Consumer;

internal sealed class LocationStoreDelayState
{
    private int _delayMilliseconds;

    public int DelayMilliseconds => Volatile.Read(ref _delayMilliseconds);

    public void SetDelay(TimeSpan delay)
    {
        var milliseconds = (int)Math.Clamp(delay.TotalMilliseconds, 0, 5000);
        Volatile.Write(ref _delayMilliseconds, milliseconds);
    }
}

internal sealed class DelayableLocationStore(
    IZLinkLocationStore inner,
    LocationStoreDelayState delayState,
    IZLinkLocationChangeStampStore? changeStampStore = null) :
    IZLinkLocationStore,
    IZLinkLocationChangeStampStore
{
    private async ValueTask WaitDelayAsync(CancellationToken cancellationToken)
    {
        var delay = delayState.DelayMilliseconds;
        if (delay > 0)
        {
            await Task.Delay(delay, cancellationToken);
        }
    }

    public async ValueTask<ZLinkLocationWriteResult> UpdatePeerAsync(
        ZLinkPeerLocation peer,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.UpdatePeerAsync(peer, intent, cancellationToken);
    }

    public async ValueTask<ZLinkLocationWriteResult> RemovePeerAsync(
        ZLinkPeerLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.RemovePeerAsync(key, owner, cancellationToken);
    }

    public async ValueTask<IReadOnlyList<ZLinkPeerLocation>> ListPeersAsync(
        ZLinkPeerLocationFilter filter,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.ListPeersAsync(filter, cancellationToken);
    }

    public async ValueTask<ZLinkLocationWriteResult> UpdateSpotAsync(
        ZLinkSpotLocation spot,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.UpdateSpotAsync(spot, intent, cancellationToken);
    }

    public async ValueTask<ZLinkLocationWriteResult> RemoveSpotAsync(
        ZLinkSpotLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.RemoveSpotAsync(key, owner, cancellationToken);
    }

    public async ValueTask<ZLinkSpotLocation?> ResolveSpotAsync(
        ZLinkSpotLocationKey key,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.ResolveSpotAsync(key, cancellationToken);
    }

    public async ValueTask<ZLinkLocationPage<ZLinkSpotLocation>> ListSpotsAsync(
        ZLinkSpotLocationFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.ListSpotsAsync(filter, page, cancellationToken);
    }

    public async ValueTask<ZLinkLocationWriteResult> UpdateActorAsync(
        ZLinkActorLocation actor,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.UpdateActorAsync(actor, intent, cancellationToken);
    }

    public async ValueTask<ZLinkLocationWriteResult> RemoveActorAsync(
        ZLinkActorLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.RemoveActorAsync(key, owner, cancellationToken);
    }

    public async ValueTask<ZLinkActorLocation?> ResolveActorAsync(
        ZLinkActorLocationKey key,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.ResolveActorAsync(key, cancellationToken);
    }

    public async ValueTask<ZLinkLocationPage<ZLinkActorLocation>> ListActorsAsync(
        ZLinkActorLocationFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.ListActorsAsync(filter, page, cancellationToken);
    }

    public async ValueTask<ZLinkLocationWriteResult> UpdateRouteAsync(
        ZLinkRouteLocation route,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.UpdateRouteAsync(route, intent, cancellationToken);
    }

    public async ValueTask<ZLinkLocationWriteResult> RemoveRouteAsync(
        ZLinkRouteLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.RemoveRouteAsync(key, owner, cancellationToken);
    }

    public async ValueTask<ZLinkRouteLocation?> ResolveRouteAsync(
        ZLinkRouteLocationKey key,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.ResolveRouteAsync(key, cancellationToken);
    }

    public async ValueTask<ZLinkLocationPage<ZLinkRouteLocation>> ListRoutesAsync(
        ZLinkRouteLocationFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.ListRoutesAsync(filter, page, cancellationToken);
    }

    public async ValueTask<ZLinkOwnerLeaseRenewal> RenewOwnerLeaseAsync(
        string ownerId,
        RoutingId nodeRid,
        TimeSpan leaseTtl,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.RenewOwnerLeaseAsync(ownerId, nodeRid, leaseTtl, cancellationToken);
    }

    public async ValueTask<bool> RemoveOwnerLeaseAsync(
        string ownerId,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.RemoveOwnerLeaseAsync(ownerId, cancellationToken);
    }

    public async ValueTask<ZLinkOwnerLeaseSnapshot> ListOwnerLeasesAsync(
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.ListOwnerLeasesAsync(cancellationToken);
    }

    public async ValueTask<long> RemoveAllByOwnerAsync(
        string ownerId,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.RemoveAllByOwnerAsync(ownerId, cancellationToken);
    }

    public async ValueTask<long> GetChangeStampAsync(
        ZLinkLocationChangeStampScope scope,
        CancellationToken cancellationToken = default)
    {
        if (changeStampStore is null)
        {
            return 0;
        }

        await WaitDelayAsync(cancellationToken);
        return await changeStampStore.GetChangeStampAsync(scope, cancellationToken);
    }
}
