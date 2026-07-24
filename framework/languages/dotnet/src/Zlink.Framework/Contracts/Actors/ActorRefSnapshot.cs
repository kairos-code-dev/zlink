using System.Text.Json.Serialization;
using Zlink.Framework.Runtime.Codecs;

namespace Zlink.Framework.Contracts.Actors;

[JsonConverter(typeof(ActorRefSnapshotJsonConverter))]
public sealed record ActorRefSnapshot(
    RoutingId NodeRid,
    string ActorId,
    ulong Generation)
{
    public static ActorRefSnapshot From(ActorRef actorRef) =>
        new(actorRef.NodeRid, actorRef.ActorId, actorRef.Generation);

    public ActorRef ToActorRef() => new(NodeRid, ActorId, Generation);
}
