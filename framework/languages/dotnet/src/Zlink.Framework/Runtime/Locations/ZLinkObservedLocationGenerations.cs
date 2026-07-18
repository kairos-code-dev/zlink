namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Highest logical version this runtime has accepted per location key, shared
/// by every read surface (resolvers and the runtime query). A read whose
/// version is strictly older than the recorded one is a lagging replica view
/// and never counts as a success result. Entries remain for the runtime
/// lifetime because forgetting one would allow a lagging replica value to
/// become current again.
///
/// Per the location runtime contract the monotonic axes are the rows' own
/// spec fields: descriptors order by (lifecycle generation, descriptor
/// revision), spot rows by spot generation, and actor rows by
/// (actor generation, membership epoch).
/// </summary>
internal sealed class ZLinkObservedLocationGenerations
{
    private readonly Observed<ZLinkMeshNodeDescriptorKey> _meshNodes = new();
    private readonly Observed<ZLinkSpotLocationKey> _spots = new();
    private readonly Observed<ZLinkActorLocationKey> _actors = new();

    internal bool AcceptDescriptor(ZLinkMeshNodeDescriptor row) =>
        _meshNodes.Accept(
            new ZLinkMeshNodeDescriptorKey(row.MeshName, row.Rid),
            new ObservedVersion(row.LifecycleGeneration, row.DescriptorRevision));

    internal void ObserveDescriptor(ZLinkMeshNodeDescriptor row) =>
        _meshNodes.Observe(
            new ZLinkMeshNodeDescriptorKey(row.MeshName, row.Rid),
            new ObservedVersion(row.LifecycleGeneration, row.DescriptorRevision));

    internal bool AcceptSpot(ZLinkSpotLocation row) =>
        _spots.Accept(
            new ZLinkSpotLocationKey(row.MeshName, row.SpotRid),
            new ObservedVersion(row.SpotGeneration, 0));

    internal void ObserveSpot(ZLinkSpotLocation row) =>
        _spots.Observe(
            new ZLinkSpotLocationKey(row.MeshName, row.SpotRid),
            new ObservedVersion(row.SpotGeneration, 0));

    internal bool AcceptActor(ZLinkActorLocation row) =>
        _actors.Accept(
            new ZLinkActorLocationKey(row.MeshName, row.ActorId),
            new ObservedVersion(row.ActorRef.Generation, row.MembershipEpoch));

    internal void ObserveActor(ZLinkActorLocation row) =>
        _actors.Observe(
            new ZLinkActorLocationKey(row.MeshName, row.ActorId),
            new ObservedVersion(row.ActorRef.Generation, row.MembershipEpoch));

    private readonly record struct ObservedVersion(ulong Major, ulong Minor)
        : IComparable<ObservedVersion>
    {
        public int CompareTo(ObservedVersion other)
        {
            var major = Major.CompareTo(other.Major);
            return major != 0 ? major : Minor.CompareTo(other.Minor);
        }
    }

    private sealed class Observed<TKey>
        where TKey : notnull
    {
        private readonly object _gate = new();
        private readonly Dictionary<TKey, ObservedVersion> _versions = [];

        internal bool Accept(TKey key, ObservedVersion version)
        {
            lock (_gate)
            {
                if (_versions.TryGetValue(key, out var observed)
                    && version.CompareTo(observed) < 0)
                {
                    return false;
                }

                _versions[key] = version;
                return true;
            }
        }

        internal void Observe(TKey key, ObservedVersion version)
        {
            lock (_gate)
            {
                if (!_versions.TryGetValue(key, out var observed)
                    || version.CompareTo(observed) > 0)
                    _versions[key] = version;
            }
        }
    }
}
