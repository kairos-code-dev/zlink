using System.Text.Json.Serialization;
using Zlink.Framework.Runtime.Codecs;

namespace Zlink.Framework.Contracts.Actors;

public interface IZLinkActorDirectory
{
    ValueTask<ActorRef?> FindAsync(
        string actorId,
        CancellationToken cancellationToken = default);

    ValueTask<ActorRef> EnsureAsync(
        string actorId,
        ZLinkMessage createRequest,
        ZLinkActorPlacement placement = default,
        CancellationToken cancellationToken = default);
}

public readonly record struct ZLinkActorPlacement(
    RoutingId? PreferredNodeRid = null,
    string? RouteMesh = null);

[JsonConverter(typeof(ActorRefSnapshotJsonConverter))]
public sealed record ActorRefSnapshot(
    RoutingId NodeRid,
    string ActorId,
    ulong Generation)
{
    public static ActorRefSnapshot From(ActorRef actorRef)
    {
        return new ActorRefSnapshot(
            actorRef.NodeRid,
            actorRef.ActorId,
            actorRef.Generation);
    }

    public ActorRef ToActorRef()
    {
        return new ActorRef(NodeRid, ActorId, Generation);
    }
}
