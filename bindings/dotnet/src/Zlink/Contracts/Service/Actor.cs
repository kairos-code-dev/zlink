// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace Systems.Zlink;

public readonly struct ActorRef : IEquatable<ActorRef>
{
    public ActorRef(RoutingId nodeRid, string actorId, ulong generation)
    {
        ActorInterop.ValidateActorId(actorId, nameof(actorId));
        NodeRid = nodeRid;
        ActorId = actorId;
        Generation = generation;
    }

    public RoutingId NodeRid { get; }
    public string ActorId { get; }
    public ulong Generation { get; }
    public bool IsUnchecked => Generation == 0;

    internal static ActorRef Unchecked(RoutingId nodeRid, string actorId)
        => new(nodeRid, actorId, 0);

    internal static ActorRef Remote(RoutingId targetNodeRid, string actorId)
        => Unchecked(targetNodeRid, actorId);

    public bool Equals(ActorRef other)
        => Generation == other.Generation
            && NodeRid.Equals(other.NodeRid)
            && string.Equals(ActorId, other.ActorId, StringComparison.Ordinal);

    public override bool Equals(object? obj)
        => obj is ActorRef other && Equals(other);

    public override int GetHashCode() => HashCode.Combine(NodeRid, ActorId,
        Generation);

    public static bool operator ==(ActorRef left, ActorRef right)
        => left.Equals(right);

    public static bool operator !=(ActorRef left, ActorRef right)
        => !left.Equals(right);
}

public sealed record ActorRoute(ActorRef Actor, RoutingId CurrentSpotRid,
    SpotKind CurrentSpotKind);

public sealed record ActorRecvInfo(ActorRef Actor, RoutingId SourceNodeRid,
    RoutingId SourceSessionRid, uint Flags);

public sealed record ActorJoinInfo(ActorRef SourceActor, ActorRef TargetActor,
    RoutingId SourceNodeRid, RoutingId SourceSpotRid,
    RoutingId TargetNodeRid, RoutingId TargetSpotRid, ulong JoinEpoch,
    uint Flags);

public sealed record SpotActorLifecycleInfo(ActorRef PreviousActor,
    ActorRef CurrentActor, RoutingId? PreviousSpotRid,
    RoutingId? CurrentSpotRid, ulong JoinEpoch, uint Flags);

public enum SpotActorLifecycleEventKind
{
    Joined = 1,
    Left = 2
}

public sealed record SpotActorLifecycleEvent(
    SpotActorLifecycleEventKind Kind,
    SpotActorLifecycleInfo Info);

public sealed record ActorReceived(ActorRecvInfo Info,
    IReadOnlyList<Message> Parts) : IDisposable
{
    private int _closed;

    public Message Message => FirstPart();

    public Message FirstPart()
    {
        return Parts.Count > 0
            ? Parts[0]
            : throw new InvalidOperationException("Actor message has no parts.");
    }

    public Message SinglePartOrThrow()
    {
        if (Parts.Count != 1)
            throw new InvalidOperationException(
                "Actor message does not contain exactly one part.");
        return Parts[0];
    }

    public void Dispose()
    {
        if (Interlocked.Exchange(ref _closed, 1) != 0)
            return;
        foreach (Message part in Parts)
            part.Dispose();
    }
}

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

    public ActorJoinInfo Info { get; }
    public Message Message { get; }
    public IReadOnlyList<Message> Parts { get; }
    internal object? RuntimeState { get; }
}

public interface IActor : IDisposable, IAsyncDisposable
{
    ActorRef Ref { get; }

    ActorJoinOperation Join(ISpot spot);

    ActorLeaveOperation Leave(ISpot spot);

    ActorReceived? Recv(RecvFlags flags = RecvFlags.None);

    SendOperation SendBoundSession();

    void CloseBoundSession(TimeSpan timeout = default);

    void Close(TimeSpan timeout = default);
}
