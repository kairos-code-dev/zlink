using System.Collections.Concurrent;

namespace Zlink.Framework.Runtime.Backend.DotNet.Wrappers;

// The framework-owned subject view for a MeshNode: Core surfaces no subject
// table, but every spot subscription passes through the spot seam, so the
// node tracks (spot, topic) pairs for the monitoring snapshot (spec 50).
internal sealed class ZLinkSpotSubscriptionTracker
{
    private readonly ConcurrentDictionary<RoutingId, ConcurrentDictionary<string, byte>> _topics = new();

    public void Add(RoutingId spotRid, string topic)
    {
        _topics.GetOrAdd(spotRid, static _ => new ConcurrentDictionary<string, byte>(StringComparer.Ordinal))
            .TryAdd(topic, 0);
    }

    public void RemoveSpot(RoutingId spotRid)
    {
        _topics.TryRemove(spotRid, out _);
    }

    public IReadOnlyList<ZLinkSpotNodeSubjectEntry> Snapshot()
    {
        var entries = new List<ZLinkSpotNodeSubjectEntry>();
        foreach (var (_, topics) in _topics)
        foreach (var topic in topics.Keys)
            entries.Add(new ZLinkSpotNodeSubjectEntry(
                ZLinkSpotRole.Sub,
                topic,
                ZLinkSubjectKind.Topic,
                ReadyPeerCount: 0,
                ActivePeerCount: 0,
                LastChangedMs: 0));
        return entries;
    }
}
