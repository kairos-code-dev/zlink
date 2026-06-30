// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
///     A reference to an actor: the node that hosts it, its actor id, and its
///     generation.
/// </summary>
public readonly partial struct ActorRef : IEquatable<ActorRef>
{
    /// <summary>
    ///     Creates an actor reference for the given host node, actor id, and
    ///     generation.
    /// </summary>
    public ActorRef(RoutingId nodeRid, string actorId, ulong generation)
    {
        ActorInterop.ValidateActorId(actorId, nameof(actorId));
        NodeRid = nodeRid;
        ActorId = actorId;
        Generation = generation;
    }

    /// <summary>
    ///     Gets the node routing id.
    /// </summary>
    public RoutingId NodeRid { get; }

    /// <summary>
    ///     Gets the actor id.
    /// </summary>
    public string ActorId { get; }

    /// <summary>
    ///     Gets the generation.
    /// </summary>
    public ulong Generation { get; }

    /// <summary>
    ///     Gets whether this reference omits generation checking (generation 0).
    /// </summary>
    public bool IsUnchecked => Generation == 0;

    /// <summary>
    ///     Returns true when <paramref name="other" /> has the same node, actor id,
    ///     and generation.
    /// </summary>
    public bool Equals(ActorRef other)
    {
        return Generation == other.Generation
               && NodeRid.Equals(other.NodeRid)
               && string.Equals(ActorId, other.ActorId, StringComparison.Ordinal);
    }

    /// <summary>
    ///     Returns true when <paramref name="obj" /> is an actor reference equal to
    ///     this one.
    /// </summary>
    public override bool Equals(object? obj)
    {
        return obj is ActorRef other && Equals(other);
    }

    /// <summary>
    ///     Returns a hash over the node, actor id, and generation.
    /// </summary>
    public override int GetHashCode()
    {
        return HashCode.Combine(NodeRid, ActorId,
            Generation);
    }

    /// <summary>
    ///     Returns true when both references are equal.
    /// </summary>
    public static bool operator ==(ActorRef left, ActorRef right)
    {
        return left.Equals(right);
    }

    /// <summary>
    ///     Returns true when the references differ.
    /// </summary>
    public static bool operator !=(ActorRef left, ActorRef right)
    {
        return !left.Equals(right);
    }
}

/// <summary>
///     The resolved route to an actor: which spot it currently lives on.
/// </summary>
/// <param name="Actor">The actor.</param>
/// <param name="CurrentSpotRid">The routing id of the spot the actor is currently on.</param>
/// <param name="CurrentSpotKind">The kind of the current spot.</param>
public sealed record ActorRoute(
    ActorRef Actor,
    RoutingId CurrentSpotRid,
    SpotKind CurrentSpotKind);

/// <summary>
///     Metadata about a message received for an actor.
/// </summary>
/// <param name="Actor">The actor the message was received for.</param>
/// <param name="SourceNodeRid">The routing id of the source node.</param>
/// <param name="SourceSessionRid">The routing id of the source session.</param>
/// <param name="Flags">Implementation-defined receive flags.</param>
public sealed record ActorRecvInfo(
    ActorRef Actor,
    RoutingId SourceNodeRid,
    RoutingId SourceSessionRid,
    uint Flags);

/// <summary>
///     Details of an actor-join request: the actors and spots on each side.
/// </summary>
/// <param name="SourceActor">The actor requesting the join.</param>
/// <param name="TargetActor">The actor being joined.</param>
/// <param name="SourceNodeRid">The routing id of the source node.</param>
/// <param name="SourceSpotRid">The routing id of the source spot.</param>
/// <param name="TargetNodeRid">The routing id of the target node.</param>
/// <param name="TargetSpotRid">The routing id of the target spot.</param>
/// <param name="JoinEpoch">The join epoch (generation) of this join.</param>
/// <param name="Flags">Implementation-defined join flags.</param>
public sealed record ActorJoinInfo(
    ActorRef SourceActor,
    ActorRef TargetActor,
    RoutingId SourceNodeRid,
    RoutingId SourceSpotRid,
    RoutingId TargetNodeRid,
    RoutingId TargetSpotRid,
    ulong JoinEpoch,
    uint Flags);

/// <summary>
///     Details of an actor lifecycle change, before and after.
/// </summary>
/// <param name="PreviousActor">The actor before the change.</param>
/// <param name="CurrentActor">The actor after the change.</param>
/// <param name="PreviousSpotRid">The routing id of the spot before the change, if any.</param>
/// <param name="CurrentSpotRid">The routing id of the spot after the change, if any.</param>
/// <param name="JoinEpoch">The join epoch (generation) involved.</param>
/// <param name="Flags">Implementation-defined lifecycle flags.</param>
public sealed record SpotActorLifecycleInfo(
    ActorRef PreviousActor,
    ActorRef CurrentActor,
    RoutingId? PreviousSpotRid,
    RoutingId? CurrentSpotRid,
    ulong JoinEpoch,
    uint Flags);

/// <summary>
///     Whether an actor joined or left a spot.
/// </summary>
public enum SpotActorLifecycleEventKind
{
    /// <summary>
    ///     The actor joined a spot.
    /// </summary>
    Joined = 1,

    /// <summary>
    ///     The actor left a spot.
    /// </summary>
    Left = 2
}

/// <summary>
///     An actor join/leave lifecycle event observed on a spot.
/// </summary>
/// <param name="Kind">Whether the actor joined or left.</param>
/// <param name="Info">Details of the actor and spots involved.</param>
public sealed record SpotActorLifecycleEvent(
    SpotActorLifecycleEventKind Kind,
    SpotActorLifecycleInfo Info) : IDisposable
{
    /// <summary>
    ///     Gets the request parts supplied when the actor was created. The event
    ///     owns these messages and disposes them when disposed.
    /// </summary>
    public IReadOnlyList<Message> RequestParts { get; init; } =
        Array.Empty<Message>();

    /// <summary>
    ///     Releases any request parts owned by this lifecycle event.
    /// </summary>
    public void Dispose()
    {
        foreach (var part in RequestParts)
            part.Dispose();
    }
}

/// <summary>
///     A message received for an actor: its metadata and parts. Owns its parts
///     until disposed.
/// </summary>
/// <param name="Info">Metadata about the received message.</param>
/// <param name="Parts">The message parts, owned by this envelope.</param>
public sealed record ActorReceived(
    ActorRecvInfo Info,
    IReadOnlyList<Message> Parts) : IDisposable
{
    private int _closed;

    /// <summary>
    ///     Returns the first message part without transferring ownership.
    /// </summary>
    public Message Message => FirstPart();

    /// <summary>
    ///     Releases resources owned by this instance.
    /// </summary>
    public void Dispose()
    {
        if (Interlocked.Exchange(ref _closed, 1) != 0)
            return;
        foreach (var part in Parts)
            part.Dispose();
    }

    /// <summary>
    ///     Returns the first message part, or throws when there are none; the part
    ///     stays owned by this envelope.
    /// </summary>
    public Message FirstPart()
    {
        return Parts.Count > 0
            ? Parts[0]
            : throw new InvalidOperationException("Actor message has no parts.");
    }

    /// <summary>
    ///     Returns the only message part, or throws unless there is exactly one;
    ///     the part stays owned by this envelope.
    /// </summary>
    public Message SinglePartOrThrow()
    {
        if (Parts.Count != 1)
            throw new InvalidOperationException(
                "Actor message does not contain exactly one part.");
        return Parts[0];
    }
}

/// <summary>
///     A pending request from an actor to join a spot, awaiting a reply.
/// </summary>
public sealed partial class ActorJoinRequest
{
    /// <summary>
    ///     Gets the info.
    /// </summary>
    public ActorJoinInfo Info { get; }

    /// <summary>
    ///     Gets the message.
    /// </summary>
    public Message Message { get; }

    /// <summary>
    ///     Gets the parts.
    /// </summary>
    public IReadOnlyList<Message> Parts { get; }
}

/// <summary>
///     A resource handle to an actor: join/leave spots, receive its messages, and
///     send to its bound session.
/// </summary>
public interface IActor : IDisposable, IAsyncDisposable
{
    /// <summary>
    ///     Gets the actor reference.
    /// </summary>
    ActorRef Ref { get; }

    /// <summary>
    ///     Begins joining the actor to <paramref name="spot" />; submit the returned
    ///     operation to apply it.
    /// </summary>
    ActorJoinOperation Join(ISpot spot);

    /// <summary>
    ///     Begins leaving <paramref name="spot" />; submit the returned operation to
    ///     apply it.
    /// </summary>
    ActorLeaveOperation Leave(ISpot spot);

    /// <summary>
    ///     Receives the next message for this actor, or null when none is available.
    /// </summary>
    ActorReceived? Recv(RecvFlags flags = RecvFlags.None);

    /// <summary>
    ///     Begins a send to the actor's bound session; parts are consumed on a
    ///     successful submit (see <see cref="SendOperation" />).
    /// </summary>
    SendOperation SendBoundSession();

    /// <summary>
    ///     Closes the actor's bound session, waiting up to
    ///     <paramref name="timeout" /> for it to drain.
    /// </summary>
    void CloseBoundSession(TimeSpan timeout = default);

    /// <summary>
    ///     Closes the actor, waiting up to <paramref name="timeout" /> for it to
    ///     drain.
    /// </summary>
    void Close(TimeSpan timeout = default);
}