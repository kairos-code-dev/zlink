using Bingo.Server.Play.Actors;
using Bingo.Shared.Configuration;
using Bingo.Shared.Contracts;
using Zlink.Framework.Contracts.Actors;

namespace Bingo.Server.Play;

[ZLinkHandlerGroup("play")]
internal sealed class EnsurePlayerActorHandler(IZLinkActorManager actors)
    : IZLinkRequestHandler<EnsurePlayerActorReq, EnsurePlayerActorRes>
{
    public async ValueTask<EnsurePlayerActorRes> HandleAsync(
        EnsurePlayerActorReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        var actor = await actors.GetOrCreateAsync(
                request.ActorId,
                SampleNames.PlayerActorType,
                cancellationToken)
            .ConfigureAwait(false);
        if (actor is PlayerActor player)
        {
            player.SetDisplayName(request.DisplayName);
        }

        var route = await actors.GetRouteAsync(
                request.ActorId,
                SampleNames.PlayerActorType,
                cancellationToken)
            .ConfigureAwait(false);
        return new EnsurePlayerActorRes(
            request.ActorId,
            new ActorRouteSnapshot(
                route.RouterChannelId,
                route.TargetNodeRid.ToBytes().ToArray(),
                route.ActorGeneration));
    }
}
