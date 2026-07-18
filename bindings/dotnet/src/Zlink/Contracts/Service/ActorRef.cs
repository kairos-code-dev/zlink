// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
///     A reference to an actor: the node that hosts it, its actor id, and its
///     generation. Maps to the native <c>zlink_actor_ref_t</c>.
/// </summary>
public readonly struct ActorRef : IEquatable<ActorRef>
{
    /// <summary>
    ///     Creates an actor reference for the given host node, actor id, and
    ///     generation.
    /// </summary>
    public ActorRef(RoutingId nodeRid, string actorId, ulong generation)
    {
        ActorContractValidation.ValidateActorId(actorId, nameof(actorId));
        NodeRid = nodeRid;
        ActorId = actorId;
        Generation = generation;
    }

    /// <summary>Gets the routing id of the node that hosts the actor.</summary>
    public RoutingId NodeRid { get; }

    /// <summary>Gets the actor id.</summary>
    public string ActorId { get; }

    /// <summary>Gets the actor generation (0 means generation-unchecked).</summary>
    public ulong Generation { get; }

    /// <summary>Gets whether this reference omits generation checking.</summary>
    public bool IsUnchecked => Generation == 0;

    /// <summary>Creates a generation-unchecked reference.</summary>
    internal static ActorRef Unchecked(RoutingId nodeRid, string actorId)
    {
        return new ActorRef(nodeRid, actorId, 0);
    }

    /// <summary>Creates a reference to an actor hosted on a remote node.</summary>
    internal static ActorRef Remote(RoutingId targetNodeRid, string actorId)
    {
        return Unchecked(targetNodeRid, actorId);
    }

    /// <inheritdoc />
    public bool Equals(ActorRef other)
    {
        return Generation == other.Generation
               && NodeRid.Equals(other.NodeRid)
               && string.Equals(ActorId, other.ActorId, StringComparison.Ordinal);
    }

    /// <inheritdoc />
    public override bool Equals(object? obj)
    {
        return obj is ActorRef other && Equals(other);
    }

    /// <inheritdoc />
    public override int GetHashCode()
    {
        return HashCode.Combine(NodeRid, ActorId, Generation);
    }

    /// <summary>Returns true when both references are equal.</summary>
    public static bool operator ==(ActorRef left, ActorRef right)
    {
        return left.Equals(right);
    }

    /// <summary>Returns true when the references differ.</summary>
    public static bool operator !=(ActorRef left, ActorRef right)
    {
        return !left.Equals(right);
    }
}

/// <summary>
///     The resolved location of an actor: which spot it lives on and the
///     membership epoch. Maps to <c>zlink_actor_location_t</c>.
/// </summary>
/// <param name="Actor">The located actor.</param>
/// <param name="SpotRid">The routing id of the spot hosting the actor.</param>
/// <param name="SpotGeneration">The generation of the hosting spot.</param>
/// <param name="MembershipEpoch">The actor's membership epoch on the spot.</param>
public sealed record ActorLocation(
    ActorRef Actor,
    RoutingId SpotRid,
    ulong SpotGeneration,
    ulong MembershipEpoch);
