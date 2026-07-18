namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Default resolvers reading the registered stores. Every read reaches the
/// store — there is no resolver cache (location runtime contract §8) —
/// and joins owner liveness plus the monotonic generation guard before a
/// row counts as live. Spot and actor rows are exposed internally for the
/// address resolvers and lifecycle flows; the public messaging surfaces
/// live on <see cref="ZLinkLocationAddressResolvers"/>.
/// </summary>
internal sealed class ZLinkStoreLocationResolvers :
    IZLinkMeshNodeLocationResolver
{
    private readonly IZLinkMeshNodeLocationStore _meshNodeStore;
    private readonly IZLinkSpotLocationStore _spotStore;
    private readonly IZLinkActorLocationStore _actorStore;
    private readonly ZLinkLocationEventEmitter _events;
    private readonly ZLinkObservedLocationGenerations _observed;
    private readonly ZLinkLiveLocationRows _liveRows;
    private readonly ZLinkLocationStoreHealth? _health;

    internal ZLinkStoreLocationResolvers(
        IZLinkMeshNodeLocationStore meshNodeStore,
        IZLinkSpotLocationStore spotStore,
        IZLinkActorLocationStore actorStore,
        ZLinkOwnerLeaseTracker leaseTracker,
        ZLinkObservedLocationGenerations observed,
        ZLinkLocationEventEmitter? events = null,
        ZLinkLocationStoreHealth? health = null,
        ZLinkLocationOptions? options = null)
    {
        _meshNodeStore = meshNodeStore;
        _spotStore = spotStore;
        _actorStore = actorStore;
        _events = events ?? ZLinkLocationEventEmitter.Disabled;
        _observed = observed;
        _health = health;
        _liveRows = new ZLinkLiveLocationRows(leaseTracker);
    }

    public async ValueTask<IReadOnlyList<ZLinkMeshNodeDescriptor>> ListLiveMeshNodesAsync(
        string meshName,
        CancellationToken cancellationToken = default)
    {
        var rows = await ZLinkLocationStoreRead.ExecuteAsync(
            _health,
            "mesh-node-resolver-read",
            cancellationToken,
            storeToken => _meshNodeStore.ListMeshNodesAsync(meshName, storeToken))
            .ConfigureAwait(false);

        // The shared acceptance policy rejects lagging lifecycle generation
        // and descriptor revision views.
        return await _liveRows.FilterAsync(
                rows,
                static row => row.OwnerId,
                _observed.AcceptDescriptor,
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask<ZLinkSpotLocation?> ResolveSpotRowAsync(
        ZLinkSpotLocationKey key,
        CancellationToken cancellationToken = default)
    {
        var row = await ResolveAsync(
            key,
            static (store, key, ct) => store.ResolveSpotAsync(key, ct),
            _spotStore,
            static row => row.OwnerId,
            row => _observed.AcceptSpot(row),
            cancellationToken).ConfigureAwait(false);
        if (row is null)
        {
            await _events.SpotResolveMissAsync(key, cancellationToken).ConfigureAwait(false);
        }

        return row;
    }

    internal async ValueTask<ZLinkActorLocation?> ResolveActorRowAsync(
        ZLinkActorLocationKey key,
        CancellationToken cancellationToken = default)
    {
        var raw = await ZLinkLocationStoreRead.ExecuteAsync(
            _health,
            "ZLinkActorLocation-resolver-read",
            cancellationToken,
            storeToken => _actorStore.ResolveActorAsync(key, storeToken)).ConfigureAwait(false);
        // A confirmed store miss ends the identity's observed lifecycle: a
        // re-created actor restarts its generation axes and must resolve.
        if (raw is null) _observed.ForgetActor(key);
        if (Environment.GetEnvironmentVariable("ZLINK_DEBUG_PUMP") == "1" && raw is not null)
            Console.Error.WriteLine(
                $"[resolve-actor] key={key.MeshName}/{key.ActorId} gen={raw.ActorRef.Generation} epoch={raw.MembershipEpoch} owner={raw.OwnerId}");
        var row = await _liveRows.ResolveAsync(
            raw,
            static row => row.OwnerId,
            // Reference generation 0 marks a claimed-but-unpublished actor:
            // the claim precedes activation, so such a row is never a
            // resolvable reference (40-location-runtime §6).
            row => row.ActorRef.Generation > 0 && _observed.AcceptActor(row),
            cancellationToken).ConfigureAwait(false);
        if (row is null)
        {
            if (Environment.GetEnvironmentVariable("ZLINK_DEBUG_PUMP") == "1")
                Console.Error.WriteLine(
                    $"[resolve-actor] miss key={key.MeshName}/{key.ActorId} raw={(raw is null ? "none" : "hidden")}");
            await _events.ActorResolveMissAsync(key, cancellationToken).ConfigureAwait(false);
        }

        return row;
    }

    private async ValueTask<TRow?> ResolveAsync<TStore, TKey, TRow>(
        TKey key,
        Func<TStore, TKey, CancellationToken, ValueTask<TRow?>> resolve,
        TStore store,
        Func<TRow, string> ownerOf,
        Func<TRow, bool> acceptObserved,
        CancellationToken cancellationToken)
        where TKey : notnull
        where TRow : class
    {
        return await _liveRows.ResolveAsync(
                await ZLinkLocationStoreRead.ExecuteAsync(
                    _health,
                    $"{typeof(TRow).Name}-resolver-read",
                    cancellationToken,
                    storeToken => resolve(store, key, storeToken)).ConfigureAwait(false),
                ownerOf,
                acceptObserved,
                cancellationToken)
            .ConfigureAwait(false);
    }

}
