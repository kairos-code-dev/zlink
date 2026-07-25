namespace Systems.Zlink;

/// <summary>
/// Identifies one exact Actor incarnation and the owner route observed with it.
/// </summary>
public readonly record struct ActorRef
{
    public ActorRef(
        string actorId,
        ulong objectGeneration,
        string meshName,
        RoutingId nodeRid)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(actorId);
        if (System.Text.Encoding.UTF8.GetByteCount(actorId) > 255)
            throw new ArgumentOutOfRangeException(nameof(actorId));
        if (objectGeneration is 0 or > long.MaxValue)
            throw new ArgumentOutOfRangeException(nameof(objectGeneration));
        ArgumentException.ThrowIfNullOrWhiteSpace(meshName);
        if (nodeRid.IsEmpty)
            throw new ArgumentException(
                "Actor owner routing id must not be empty.",
                nameof(nodeRid));

        ActorId = actorId;
        ObjectGeneration = objectGeneration;
        MeshName = meshName;
        NodeRid = nodeRid;
    }

    public string ActorId { get; }

    public ulong ObjectGeneration { get; }

    public string MeshName { get; }

    public RoutingId NodeRid { get; }

    public void Deconstruct(
        out string actorId,
        out ulong objectGeneration,
        out string meshName,
        out RoutingId nodeRid)
    {
        actorId = ActorId;
        objectGeneration = ObjectGeneration;
        meshName = MeshName;
        nodeRid = NodeRid;
    }
}
