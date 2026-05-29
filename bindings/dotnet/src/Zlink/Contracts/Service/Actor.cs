// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace Systems.Zlink;

/// <summary>
/// Represents actor ref.
/// </summary>
public readonly struct ActorRef : IEquatable<ActorRef>
{
    /// <summary>
    /// Creates a actor ref instance.
    /// </summary>
    public ActorRef(RoutingId nodeRid, string actorId, ulong generation)
    {
        ActorInterop.ValidateActorId(actorId, nameof(actorId));
        NodeRid = nodeRid;
        ActorId = actorId;
        Generation = generation;
    }

    /// <summary>
    /// Gets the node routing id.
    /// </summary>
    public RoutingId NodeRid { get; }
    /// <summary>
    /// Gets the actor id.
    /// </summary>
    public string ActorId { get; }
    /// <summary>
    /// Gets the generation.
    /// </summary>
    public ulong Generation { get; }
    /// <summary>
    /// Gets whether the unchecked.
    /// </summary>
    public bool IsUnchecked => Generation == 0;

    internal static ActorRef Unchecked(RoutingId nodeRid, string actorId)
        => new(nodeRid, actorId, 0);

    internal static ActorRef Remote(RoutingId targetNodeRid, string actorId)
        => Unchecked(targetNodeRid, actorId);

    /// <summary>
    /// Determines whether the values are equal.
    /// </summary>
    /// <returns>The operation result.</returns>
    public bool Equals(ActorRef other)
        => Generation == other.Generation
            && NodeRid.Equals(other.NodeRid)
            && string.Equals(ActorId, other.ActorId, StringComparison.Ordinal);

    /// <summary>
    /// Determines whether the values are equal.
    /// </summary>
    /// <returns>The operation result.</returns>
    public override bool Equals(object? obj)
        => obj is ActorRef other && Equals(other);

    /// <summary>
    /// Gets the hash code.
    /// </summary>
    /// <returns>The operation result.</returns>
    public override int GetHashCode() => HashCode.Combine(NodeRid, ActorId,
        Generation);

    /// <summary>
    /// Determines whether two values are equal.
    /// </summary>
    /// <returns>The operation result.</returns>
    public static bool operator ==(ActorRef left, ActorRef right)
        => left.Equals(right);

    /// <summary>
    /// Determines whether two values are not equal.
    /// </summary>
    /// <returns>The operation result.</returns>
    public static bool operator !=(ActorRef left, ActorRef right)
        => !left.Equals(right);
}

/// <summary>
/// Describes actor route data.
/// </summary>
/// <param name="Actor">The actor value.</param>
/// <param name="CurrentSpotRid">The current spot routing id value.</param>
/// <param name="CurrentSpotKind">The current spot kind value.</param>
public sealed record ActorRoute(ActorRef Actor, RoutingId CurrentSpotRid,
    SpotKind CurrentSpotKind);

/// <summary>
/// Describes actor receive info data.
/// </summary>
/// <param name="Actor">The actor value.</param>
/// <param name="SourceNodeRid">The source node routing id value.</param>
/// <param name="SourceSessionRid">The source session routing id value.</param>
/// <param name="Flags">The flags value.</param>
public sealed record ActorRecvInfo(ActorRef Actor, RoutingId SourceNodeRid,
    RoutingId SourceSessionRid, uint Flags);

/// <summary>
/// Describes actor join info data.
/// </summary>
/// <param name="SourceActor">The source actor value.</param>
/// <param name="TargetActor">The target actor value.</param>
/// <param name="SourceNodeRid">The source node routing id value.</param>
/// <param name="SourceSpotRid">The source spot routing id value.</param>
/// <param name="TargetNodeRid">The target node routing id value.</param>
/// <param name="TargetSpotRid">The target spot routing id value.</param>
/// <param name="JoinEpoch">The join epoch value.</param>
/// <param name="Flags">The flags value.</param>
public sealed record ActorJoinInfo(ActorRef SourceActor, ActorRef TargetActor,
    RoutingId SourceNodeRid, RoutingId SourceSpotRid,
    RoutingId TargetNodeRid, RoutingId TargetSpotRid, ulong JoinEpoch,
    uint Flags);

/// <summary>
/// Describes spot actor lifecycle info data.
/// </summary>
/// <param name="PreviousActor">The previous actor value.</param>
/// <param name="CurrentActor">The current actor value.</param>
/// <param name="PreviousSpotRid">The previous spot routing id value.</param>
/// <param name="CurrentSpotRid">The current spot routing id value.</param>
/// <param name="JoinEpoch">The join epoch value.</param>
/// <param name="Flags">The flags value.</param>
public sealed record SpotActorLifecycleInfo(ActorRef PreviousActor,
    ActorRef CurrentActor, RoutingId? PreviousSpotRid,
    RoutingId? CurrentSpotRid, ulong JoinEpoch, uint Flags);

/// <summary>
/// Defines spot actor lifecycle event kind values.
/// </summary>
public enum SpotActorLifecycleEventKind
{
    /// <summary>
    /// Indicates the joined spot actor lifecycle event kind.
    /// </summary>
    Joined = 1,
    /// <summary>
    /// Indicates the left spot actor lifecycle event kind.
    /// </summary>
    Left = 2
}

/// <summary>
/// Describes spot actor lifecycle event data.
/// </summary>
/// <param name="Kind">The kind value.</param>
/// <param name="Info">The info value.</param>
public sealed record SpotActorLifecycleEvent(
    SpotActorLifecycleEventKind Kind,
    SpotActorLifecycleInfo Info);

/// <summary>
/// Describes actor received data.
/// </summary>
/// <param name="Info">The info value.</param>
/// <param name="Parts">The parts value.</param>
public sealed record ActorReceived(ActorRecvInfo Info,
    IReadOnlyList<Message> Parts) : IDisposable
{
    private int _closed;

    /// <summary>
    /// Returns the first message part without transferring ownership.
    /// </summary>
    public Message Message => FirstPart();

    /// <summary>
    /// Returns the first message part without transferring ownership.
    /// </summary>
    /// <returns>The operation result.</returns>
    public Message FirstPart()
    {
        return Parts.Count > 0
            ? Parts[0]
            : throw new InvalidOperationException("Actor message has no parts.");
    }

    /// <summary>
    /// Returns the only message part or throws when the envelope is multipart.
    /// </summary>
    /// <returns>The operation result.</returns>
    public Message SinglePartOrThrow()
    {
        if (Parts.Count != 1)
            throw new InvalidOperationException(
                "Actor message does not contain exactly one part.");
        return Parts[0];
    }

    /// <summary>
    /// Releases resources owned by this instance.
    /// </summary>
    public void Dispose()
    {
        if (Interlocked.Exchange(ref _closed, 1) != 0)
            return;
        foreach (Message part in Parts)
            part.Dispose();
    }
}

/// <summary>
/// Represents actor join request.
/// </summary>
public sealed class ActorJoinRequest
{
    internal ActorJoinRequest(ActorJoinInfo info, Message message)
        : this(info, [message], runtimeState: null)
    {
    }

    internal ActorJoinRequest(ActorJoinInfo info, IReadOnlyList<Message> parts)
        : this(info, parts, runtimeState: null)
    {
    }

    internal ActorJoinRequest(ActorJoinInfo info, IReadOnlyList<Message> parts,
        object? runtimeState)
    {
        Info = info;
        Parts = parts;
        Message = parts.Count > 0 ? parts[0] : new Message();
        RuntimeState = runtimeState;
    }

    /// <summary>
    /// Gets the info.
    /// </summary>
    public ActorJoinInfo Info { get; }
    /// <summary>
    /// Gets the message.
    /// </summary>
    public Message Message { get; }
    /// <summary>
    /// Gets the parts.
    /// </summary>
    public IReadOnlyList<Message> Parts { get; }
    internal object? RuntimeState { get; }
}

/// <summary>
/// Defines the actor contract.
/// </summary>
public interface IActor : IDisposable, IAsyncDisposable
{
    /// <summary>
    /// Gets the actor reference.
    /// </summary>
    ActorRef Ref { get; }

    /// <summary>
    /// Starts a join operation.
    /// </summary>
    ActorJoinOperation Join(ISpot spot);

    /// <summary>
    /// Starts a leave operation.
    /// </summary>
    ActorLeaveOperation Leave(ISpot spot);

    /// <summary>
    /// Receives the next available item.
    /// </summary>
    ActorReceived? Recv(RecvFlags flags = RecvFlags.None);

    /// <summary>
    /// Starts a send operation.
    /// </summary>
    SendOperation SendBoundSession();

    /// <summary>
    /// Closes the resource.
    /// </summary>
    void CloseBoundSession(TimeSpan timeout = default);

    /// <summary>
    /// Closes the resource.
    /// </summary>
    void Close(TimeSpan timeout = default);
}
