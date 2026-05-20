using Bingo.Shared.Configuration;
using Bingo.Shared.Contracts;
using Zlink.Framework.Contracts.Actors;

namespace Bingo.Server.Play;

internal sealed class EnsurePlayerActorHandler(
    IZLinkActorManager actors,
    IZLinkActorPlayRouteResolver playRoutes)
{
    [ZLinkRequest]
    public async ValueTask<EnsurePlayerActorRes> EnsurePlayerActor(
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

        var route = await playRoutes.ResolvePlayRouteAsync(request.ActorId, cancellationToken)
            .ConfigureAwait(false);
        return new EnsurePlayerActorRes(
            request.ActorId,
            new ActorRouteSnapshot(route.RouterChannelId, route.TargetNodeRid.ToBytes().ToArray()));
    }
}
