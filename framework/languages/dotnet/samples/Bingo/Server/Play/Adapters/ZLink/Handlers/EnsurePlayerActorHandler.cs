using Systems.Zlink;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Timers;
using Bingo.Server.Play.Adapters.ZLink.Actors;
using Bingo.Server.Configuration;
using Bingo.Shared.Contracts;

namespace Bingo.Server.Play.Adapters.ZLink.Handlers;

[ZLinkHandlerGroup("play")]
internal sealed class EnsurePlayerActorHandler(
    IZLinkActorManager actors)
    : IZLinkRouteRequestHandler<EnsurePlayerActorReq, EnsurePlayerActorRes>
{
    public async ValueTask<EnsurePlayerActorRes> HandleAsync(
        EnsurePlayerActorReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        var actor = await actors.GetOrCreateAsync(
                request.ActorId,
                SampleNames.PlayerActorType,
                cancellationToken)
            ;
        if (actor is PlayerActor player)
        {
            player.SetDisplayName(request.DisplayName);
        }

        using var entryJoinRequest = Message.From(ReadOnlySpan<byte>.Empty);
        var joined = await actor.Context.JoinEntrySpot(
                RoutingId.From(request.PreferredActorNodeRid),
                entryJoinRequest)
            .Async(cancellationToken)
            ;
        return new EnsurePlayerActorRes
        {
            ActorId = request.ActorId,
            ActorType = SampleNames.PlayerActorType,
            Actor = new ActorRefSnapshot
            {
                NodeRid = joined.Actor.NodeRid.ToString(),
                ActorId = joined.Actor.ActorId,
                Generation = joined.Actor.Generation,
            },
        };
    }
}
