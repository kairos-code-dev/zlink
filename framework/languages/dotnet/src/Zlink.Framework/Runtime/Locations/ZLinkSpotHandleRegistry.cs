namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Weakly tracks the resolved spot handles handed out to messaging callers
/// so location events can update or invalidate their snapshots in place.
/// Spot handles order updates by the row's spot generation; actor handles
/// order by membership epoch, the axis that changes on every membership
/// move within one actor generation.
/// </summary>
internal sealed class ZLinkSpotHandleRegistry
{
    private readonly object _gate = new();
    private readonly Dictionary<ZLinkActorLocationKey, List<WeakReference<ZLinkResolvedSpotHandle>>> _actors = [];
    private readonly Dictionary<ZLinkSpotLocationKey, List<WeakReference<ZLinkResolvedSpotHandle>>> _spots = [];

    internal void RegisterSpot(ZLinkSpotLocationKey key, ZLinkResolvedSpotHandle handle)
        => Register(_spots, key, handle);

    internal void RegisterActor(ZLinkActorLocationKey key, ZLinkResolvedSpotHandle handle)
        => Register(_actors, key, handle);

    internal void UpdateSpot(ZLinkSpotLocation row)
        => Apply(_spots, new ZLinkSpotLocationKey(row.MeshName, row.SpotRid), handle => handle.Update(
            new ZLinkSpotHandleSnapshot(
                row.MeshName,
                row.OwnerNodeRid,
                row.SpotRid,
                row.SpotGeneration,
                row.SpotKind,
                row.AuthorityOwnerGeneration),
            row.SpotGeneration));

    internal void RemoveSpot(ZLinkSpotLocationKey key, ulong spotGeneration)
        => Apply(_spots, key, handle => handle.Invalidate(spotGeneration));

    internal void UpdateActor(ZLinkActorLocation row)
        => Apply(
            _actors,
            new ZLinkActorLocationKey(row.MeshName, row.ActorId),
            handle => handle.Update(ToSnapshot(row), row.MembershipEpoch));

    private ZLinkSpotHandleSnapshot ToSnapshot(ZLinkActorLocation row)
        => row.SpotKind == ZLinkSpotKind.Entry || row.SpotRid is not { Size: > 0 }
            ? new ZLinkSpotHandleSnapshot(
                row.MeshName,
                row.OwnerNodeRid,
                row.OwnerNodeRid,
                row.SpotGeneration,
                ZLinkSpotKind.Entry,
                row.AuthorityOwnerGeneration)
            : new ZLinkSpotHandleSnapshot(
                row.MeshName,
                row.OwnerNodeRid,
                row.SpotRid,
                row.SpotGeneration,
                ZLinkSpotKind.User,
                row.AuthorityOwnerGeneration);

    internal void RemoveActor(ZLinkActorLocationKey key)
        => Apply(_actors, key, static handle => handle.InvalidateCurrent());

    internal IReadOnlyList<ZLinkResolvedSpotHandle> SnapshotLiveHandles()
    {
        lock (_gate)
        {
            var live = new List<ZLinkResolvedSpotHandle>();
            Collect(_spots, live);
            Collect(_actors, live);
            return live;
        }
    }

    private static void Collect<TKey>(
        Dictionary<TKey, List<WeakReference<ZLinkResolvedSpotHandle>>> handles,
        List<ZLinkResolvedSpotHandle> live)
        where TKey : notnull
    {
        foreach (var entries in handles.Values)
            foreach (var entry in entries)
                if (entry.TryGetTarget(out var handle))
                    live.Add(handle);
    }

    private void Register<TKey>(
        Dictionary<TKey, List<WeakReference<ZLinkResolvedSpotHandle>>> handles,
        TKey key,
        ZLinkResolvedSpotHandle handle)
        where TKey : notnull
    {
        lock (_gate)
        {
            if (!handles.TryGetValue(key, out var entries))
            {
                entries = [];
                handles.Add(key, entries);
            }
            entries.RemoveAll(static entry => !entry.TryGetTarget(out _));
            entries.Add(new WeakReference<ZLinkResolvedSpotHandle>(handle));
        }
    }

    private void Apply<TKey>(
        Dictionary<TKey, List<WeakReference<ZLinkResolvedSpotHandle>>> handles,
        TKey key,
        Action<ZLinkResolvedSpotHandle> update)
        where TKey : notnull
    {
        lock (_gate)
        {
            if (!handles.TryGetValue(key, out var entries)) return;
            entries.RemoveAll(entry =>
            {
                if (!entry.TryGetTarget(out var handle)) return true;
                update(handle);
                return false;
            });
            if (entries.Count == 0) handles.Remove(key);
        }
    }
}
