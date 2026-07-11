namespace Zlink.Framework.Runtime.Locations;

internal sealed class ZLinkSpotHandleRegistry
{
    private readonly object _gate = new();
    private readonly Dictionary<string, List<WeakReference<ZLinkResolvedSpotHandle>>> _actors =
        new(StringComparer.Ordinal);
    private readonly Dictionary<ZLinkSpotLocationKey, List<WeakReference<ZLinkResolvedSpotHandle>>> _spots = [];

    internal void RegisterSpot(ZLinkSpotLocationKey key, ZLinkResolvedSpotHandle handle)
        => Register(_spots, key, handle);

    internal void RegisterActor(string actorId, ZLinkResolvedSpotHandle handle)
        => Register(_actors, actorId, handle);

    internal void UpdateSpot(ZLinkSpotLocation row)
        => Apply(_spots, new ZLinkSpotLocationKey(row.MeshName, row.SpotRid), handle => handle.Update(
            new ZLinkSpotHandleSnapshot(row.MeshName, row.NodeRid, row.SpotRid, row.Generation)));

    internal void RemoveSpot(ZLinkSpotLocationKey key, long generation)
        => Apply(_spots, key, handle => handle.Invalidate(generation));

    internal void MarkSpotPending(ZLinkSpotLocationKey key, long generation)
        => Apply(_spots, key, handle => handle.MarkPending(generation));

    internal void RemoveSpotIfUnchanged(ZLinkSpotLocationKey key, long generation)
        => Apply(_spots, key, handle => handle.InvalidateIfGeneration(generation));

    internal void UpdateActor(ZLinkActorLocation row)
        => Apply(_actors, row.ActorId, handle => handle.Update(
            ZLinkLocationAddressResolvers.ToSnapshot(row)));

    internal void RemoveActor(ZLinkActorLocationKey key, long generation)
        => Apply(_actors, key.ActorId, handle => handle.Invalidate(generation));

    internal void MarkActorPending(ZLinkActorLocationKey key, long generation)
        => Apply(_actors, key.ActorId, handle => handle.MarkPending(generation));

    internal void RemoveActorIfUnchanged(string actorId, long generation)
        => Apply(_actors, actorId, handle => handle.InvalidateIfGeneration(generation));

    internal (ZLinkLiveSpotHandleKey[] Spots, ZLinkLiveActorHandleKey[] Actors) SnapshotLiveKeys()
    {
        lock (_gate)
        {
            Prune(_spots);
            Prune(_actors);
            return (
                _spots.SelectMany(static pair => LiveGenerations(pair.Value).Select(
                    generation => new ZLinkLiveSpotHandleKey(
                        pair.Key.MeshName,
                        pair.Key.SpotRid,
                        generation))).ToArray(),
                _actors.SelectMany(static pair => LiveGenerations(pair.Value).Select(
                    generation => new ZLinkLiveActorHandleKey(
                        pair.Key,
                        generation))).ToArray());
        }
    }

    private static IEnumerable<long> LiveGenerations(
        IEnumerable<WeakReference<ZLinkResolvedSpotHandle>> handles)
    {
        var generations = new HashSet<long>();
        foreach (var reference in handles)
            if (reference.TryGetTarget(out var handle))
                generations.Add(handle.CurrentGeneration);
        return generations;
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

    private static void Prune<TKey>(
        Dictionary<TKey, List<WeakReference<ZLinkResolvedSpotHandle>>> handles)
        where TKey : notnull
    {
        foreach (var (key, entries) in handles.ToArray())
        {
            entries.RemoveAll(static entry => !entry.TryGetTarget(out _));
            if (entries.Count == 0) handles.Remove(key);
        }
    }
}

internal readonly record struct ZLinkLiveSpotHandleKey(
    string MeshName,
    RoutingId SpotRid,
    long Generation)
{
    internal ZLinkSpotLocationKey LocationKey => new(MeshName, SpotRid);
}

internal readonly record struct ZLinkLiveActorHandleKey(string ActorId, long Generation);
