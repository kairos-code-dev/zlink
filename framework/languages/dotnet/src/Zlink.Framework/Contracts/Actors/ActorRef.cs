namespace Systems.Zlink;

/// <summary>Identifies an actor and the generation of its current lifecycle.</summary>
public readonly struct ActorRef : IEquatable<ActorRef>
{
    public ActorRef(RoutingId nodeRid, string actorId, ulong generation)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(actorId);
        if (System.Text.Encoding.UTF8.GetByteCount(actorId) > 255)
            throw new ArgumentOutOfRangeException(nameof(actorId));

        NodeRid = nodeRid;
        ActorId = actorId;
        Generation = generation;
    }

    public RoutingId NodeRid { get; }
    public string ActorId { get; }
    public ulong Generation { get; }

    public bool Equals(ActorRef other) =>
        NodeRid.Equals(other.NodeRid)
        && Generation == other.Generation
        && string.Equals(ActorId, other.ActorId, StringComparison.Ordinal);

    public override bool Equals(object? obj) => obj is ActorRef other && Equals(other);
    public override int GetHashCode() => HashCode.Combine(NodeRid, ActorId, Generation);
    public static bool operator ==(ActorRef left, ActorRef right) => left.Equals(right);
    public static bool operator !=(ActorRef left, ActorRef right) => !left.Equals(right);
}
