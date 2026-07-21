using System.Collections.Concurrent;

namespace Zlink.Framework.Runtime.Backend.DotNet.Wrappers;

// The framework-owned subject view for a MeshNode: Core surfaces no subject
// table, but every spot subscription passes through the spot seam, so the
// node tracks (spot, topic) pairs for the monitoring snapshot (spec 50).
internal sealed class ZLinkSpotSubscriptionTracker
{
    private readonly ConcurrentDictionary<RoutingId, ConcurrentDictionary<SubscriptionKey, byte>> _targets = new();

    public void Add(RoutingId spotRid, string channelName, string topic)
    {
        _targets.GetOrAdd(spotRid, static _ => new ConcurrentDictionary<SubscriptionKey, byte>())
            .TryAdd(new SubscriptionKey(channelName, topic), 0);
    }

    public void RemoveSpot(RoutingId spotRid)
    {
        _targets.TryRemove(spotRid, out _);
    }

    public IReadOnlyList<ZLinkSpotNodeSubjectEntry> Snapshot()
    {
        var entries = new List<ZLinkSpotNodeSubjectEntry>();
        foreach (var (_, targets) in _targets)
        foreach (var target in targets.Keys)
            entries.Add(new ZLinkSpotNodeSubjectEntry(
                ZLinkSpotRole.Sub,
                $"{target.ChannelName}:{target.Topic}",
                ZLinkSubjectKind.Topic,
                ReadyPeerCount: 0,
                ActivePeerCount: 0,
                LastChangedMs: 0));
        return entries;
    }

    private readonly record struct SubscriptionKey(string ChannelName, string Topic);
}
