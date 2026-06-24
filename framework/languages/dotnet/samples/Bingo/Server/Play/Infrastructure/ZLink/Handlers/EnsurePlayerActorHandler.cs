using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Timers;
using Systems.Zlink;
using Bingo.Server.Play.Infrastructure.ZLink.Actors;
using Bingo.Server.Configuration;
using Bingo.Shared.Contracts;

namespace Bingo.Server.Play.Infrastructure.ZLink.Handlers;

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
        var joined = await actors.JoinEntrySpotAsync(
                request.ActorId,
                SampleNames.PlayerActorType,
                RoutingId.From(request.PreferredActorNodeRid),
                request,
                cancellationToken)
            ;
        if (!joined.Accepted)
        {
            throw new InvalidOperationException($"Entry spot actor join was rejected: {request.ActorId}");
        }

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
