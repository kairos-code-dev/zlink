namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Default resolvers reading the registered stores. Applies the framework
/// cache policy: single-resolve caches for spot/actor/route, a peer list
/// cache for auto connect, positive TTL only (not-found is never cached),
/// and an owner lease join before every successful return.
/// </summary>
internal sealed class ZLinkStoreLocationResolvers :
    IZLinkPeerLocationResolver,
    IZLinkSpotLocationResolver,
    IZLinkActorLocationResolver,
    IZLinkRouteLocationResolver
{
    private readonly ZLinkLocationOptions _options;
    private readonly IZLinkPeerLocationStore _peerStore;
    private readonly IZLinkSpotLocationStore _spotStore;
    private readonly IZLinkActorLocationStore _actorStore;
    private readonly IZLinkRouteLocationStore _routeStore;
    private readonly ZLinkOwnerLeaseTracker _leaseTracker;
    private readonly TimeProvider _time;
    private readonly PositiveCache<ZLinkPeerLocationFilter, IReadOnlyList<ZLinkPeerLocation>> _peerListCache;
    private readonly PositiveCache<ZLinkSpotLocationKey, ZLinkSpotLocation> _spotCache;
    private readonly PositiveCache<ZLinkActorLocationKey, ZLinkActorLocation> _actorCache;
    private readonly PositiveCache<ZLinkRouteLocationKey, ZLinkRouteLocation> _routeCache;
    private readonly ObservedGenerations<ZLinkPeerLocationKey> _observedPeers = new();
    private readonly ObservedGenerations<ZLinkSpotLocationKey> _observedSpots = new();
    private readonly ObservedGenerations<ZLinkActorLocationKey> _observedActors = new();
    private readonly ObservedGenerations<ZLinkRouteLocationKey> _observedRoutes = new();

    internal ZLinkStoreLocationResolvers(
        ZLinkLocationOptions options,
        IZLinkPeerLocationStore peerStore,
        IZLinkSpotLocationStore spotStore,
        IZLinkActorLocationStore actorStore,
        IZLinkRouteLocationStore routeStore,
        ZLinkOwnerLeaseTracker leaseTracker,
        TimeProvider? timeProvider = null)
    {
        _options = options;
        _peerStore = peerStore;
        _spotStore = spotStore;
        _actorStore = actorStore;
        _routeStore = routeStore;
        _leaseTracker = leaseTracker;
        _time = timeProvider ?? TimeProvider.System;
        _peerListCache = new PositiveCache<ZLinkPeerLocationFilter, IReadOnlyList<ZLinkPeerLocation>>(_time);
        _spotCache = new PositiveCache<ZLinkSpotLocationKey, ZLinkSpotLocation>(_time);
        _actorCache = new PositiveCache<ZLinkActorLocationKey, ZLinkActorLocation>(_time);
        _routeCache = new PositiveCache<ZLinkRouteLocationKey, ZLinkRouteLocation>(_time);
    }

    internal int PeerCacheEntryCount => _peerListCache.Count;

    internal int SpotCacheEntryCount => _spotCache.Count;

    internal int ActorCacheEntryCount => _actorCache.Count;

    internal int RouteCacheEntryCount => _routeCache.Count;

    public async ValueTask<IReadOnlyList<ZLinkPeerLocation>> ListPeersAsync(
        ZLinkPeerLocationFilter filter,
        ZLinkResolveFreshness freshness = ZLinkResolveFreshness.Normal,
        CancellationToken cancellationToken = default)
    {
        var useCache = _options.PeerCacheEnabled;
        if (useCache
            && freshness == ZLinkResolveFreshness.Normal
            && _peerListCache.TryGet(filter, _options.PositiveCacheTtl, out var cached))
        {
            return await FilterLiveAsync(cached, cancellationToken).ConfigureAwait(false);
        }

        var rows = await _peerStore.ListPeersAsync(filter, cancellationToken).ConfigureAwait(false);

        // Drop rows older than a generation this runtime already observed
        // for the same key: a lagging store replica must never roll the
        // view backwards (monotonic read guard).
        var fresh = new List<ZLinkPeerLocation>(rows.Count);
        foreach (var row in rows)
        {
            var key = new ZLinkPeerLocationKey(
                row.AutoConnectType, row.MeshName, row.Role, row.NodeRid, row.Endpoint);
            if (_observedPeers.Accept(key, row.Generation))
            {
                fresh.Add(row);
            }
        }

        if (useCache)
        {
            _peerListCache.Set(filter, fresh, _options.CacheMaxEntries);
        }

        return await FilterLiveAsync(fresh, cancellationToken).ConfigureAwait(false);
    }

    public ValueTask<ZLinkSpotLocation?> ResolveSpotAsync(
        ZLinkSpotLocationKey key,
        ZLinkResolveFreshness freshness = ZLinkResolveFreshness.Normal,
        CancellationToken cancellationToken = default) =>
        ResolveAsync(
            key,
            freshness,
            _options.SpotCacheEnabled,
            _spotCache,
            _observedSpots,
            static (store, key, ct) => store.ResolveSpotAsync(key, ct),
            _spotStore,
            static row => row.OwnerId,
            static row => row.Generation,
            cancellationToken);

    public ValueTask<ZLinkActorLocation?> ResolveActorAsync(
        ZLinkActorLocationKey key,
        ZLinkResolveFreshness freshness = ZLinkResolveFreshness.Normal,
        CancellationToken cancellationToken = default)
    {
        // An absent actor type and an empty one are the same key by contract.
        var normalized = key with
        {
            ActorType = ZLinkLocationKeyCodec.NormalizeActorType(key.ActorType)
        };
        return ResolveAsync(
            normalized,
            freshness,
            _options.ActorCacheEnabled,
            _actorCache,
            _observedActors,
            static (store, key, ct) => store.ResolveActorAsync(key, ct),
            _actorStore,
            static row => row.OwnerId,
            static row => row.Generation,
            cancellationToken);
    }

    public ValueTask<ZLinkRouteLocation?> ResolveRouteAsync(
        ZLinkRouteLocationKey key,
        ZLinkResolveFreshness freshness = ZLinkResolveFreshness.Normal,
        CancellationToken cancellationToken = default) =>
        ResolveAsync(
            key,
            freshness,
            _options.RouteCacheEnabled,
            _routeCache,
            _observedRoutes,
            static (store, key, ct) => store.ResolveRouteAsync(key, ct),
            _routeStore,
            static row => row.OwnerId,
            static row => row.Generation,
            cancellationToken);

    private async ValueTask<TRow?> ResolveAsync<TStore, TKey, TRow>(
        TKey key,
        ZLinkResolveFreshness freshness,
        bool cacheEnabled,
        PositiveCache<TKey, TRow> cache,
        ObservedGenerations<TKey> observed,
        Func<TStore, TKey, CancellationToken, ValueTask<TRow?>> resolve,
        TStore store,
        Func<TRow, string> ownerOf,
        Func<TRow, long> generationOf,
        CancellationToken cancellationToken)
        where TKey : notnull
        where TRow : class
    {
        if (cacheEnabled
            && freshness == ZLinkResolveFreshness.Normal
            && cache.TryGet(key, _options.PositiveCacheTtl, out var cached))
        {
            if (await _leaseTracker.IsOwnerLiveAsync(ownerOf(cached), cancellationToken)
                    .ConfigureAwait(false))
            {
                return cached;
            }

            cache.Remove(key);
        }

        var row = await resolve(store, key, cancellationToken).ConfigureAwait(false);
        if (row is null)
        {
            // Not-found is never cached: a create-if-absent decision right
            // after this call must be able to see a freshly claimed row.
            return null;
        }

        if (!observed.Accept(key, generationOf(row)))
        {
            // A lagging store replica returned a generation this runtime
            // already saw superseded; stale rows never count as success.
            return null;
        }

        if (!await _leaseTracker.IsOwnerLiveAsync(ownerOf(row), cancellationToken)
                .ConfigureAwait(false))
        {
            return null;
        }

        if (cacheEnabled)
        {
            cache.Set(key, row, _options.CacheMaxEntries);
        }

        return row;
    }

    private async ValueTask<IReadOnlyList<ZLinkPeerLocation>> FilterLiveAsync(
        IReadOnlyList<ZLinkPeerLocation> rows,
        CancellationToken cancellationToken)
    {
        // Liveness is joined on every read so an expired owner drops out of
        // the desired set within one polling interval even on cache hits.
        var live = new List<ZLinkPeerLocation>(rows.Count);
        foreach (var row in rows)
        {
            if (await _leaseTracker.IsOwnerLiveAsync(row.OwnerId, cancellationToken)
                    .ConfigureAwait(false))
            {
                live.Add(row);
            }
        }

        return live;
    }

    /// <summary>
    /// Highest generation this runtime has accepted per key. A read whose
    /// generation is strictly older than the recorded one is a lagging
    /// replica view and never counts as a success result. Bounded; evicting
    /// an entry only weakens the guard for that key, never correctness of
    /// fencing (the store still enforces owner/generation on writes).
    /// </summary>
    private sealed class ObservedGenerations<TKey>
        where TKey : notnull
    {
        private const int MaxEntries = 8192;
        private readonly object _gate = new();
        private readonly Dictionary<TKey, long> _generations = [];

        internal bool Accept(TKey key, long generation)
        {
            lock (_gate)
            {
                if (_generations.TryGetValue(key, out var observed) && generation < observed)
                {
                    return false;
                }

                if (!_generations.ContainsKey(key) && _generations.Count >= MaxEntries)
                {
                    _generations.Clear();
                }

                _generations[key] = generation;
                return true;
            }
        }
    }

    private sealed class PositiveCache<TKey, TValue>
        where TKey : notnull
        where TValue : class
    {
        private readonly object _gate = new();
        private readonly Dictionary<TKey, (TValue Value, long StoredAt)> _entries = [];
        private readonly TimeProvider _time;

        internal PositiveCache(TimeProvider time)
        {
            _time = time;
        }

        internal int Count
        {
            get
            {
                lock (_gate)
                {
                    return _entries.Count;
                }
            }
        }

        internal bool TryGet(TKey key, TimeSpan ttl, out TValue value)
        {
            lock (_gate)
            {
                if (_entries.TryGetValue(key, out var entry)
                    && _time.GetElapsedTime(entry.StoredAt) < ttl)
                {
                    value = entry.Value;
                    return true;
                }

                _entries.Remove(key);
                value = null!;
                return false;
            }
        }

        internal void Set(TKey key, TValue value, int maxEntries)
        {
            lock (_gate)
            {
                if (!_entries.ContainsKey(key) && _entries.Count >= maxEntries)
                {
                    EvictOldest();
                }

                _entries[key] = (value, _time.GetTimestamp());
            }
        }

        internal void Remove(TKey key)
        {
            lock (_gate)
            {
                _entries.Remove(key);
            }
        }

        private void EvictOldest()
        {
            var oldestKey = default(TKey);
            var oldestStoredAt = long.MaxValue;
            foreach (var pair in _entries)
            {
                if (pair.Value.StoredAt < oldestStoredAt)
                {
                    oldestStoredAt = pair.Value.StoredAt;
                    oldestKey = pair.Key;
                }
            }

            if (oldestKey is not null)
            {
                _entries.Remove(oldestKey);
            }
        }
    }
}
