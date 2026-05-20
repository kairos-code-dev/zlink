using TicTacToe.SessionGateway.Shared.Configuration;
using TicTacToe.SessionGateway.Shared.Contracts;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Handlers;

namespace TicTacToe.SessionActorDispatch.Play;

internal sealed class EnsurePlayerActorHandler(IZLinkActorManager actors)
{
    [ZLinkRequest]
    public async ValueTask<EnsurePlayerActorRes> EnsurePlayerActor(
        EnsurePlayerActorReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        var actorId = request.ActorId.Trim();
        if (string.IsNullOrWhiteSpace(actorId))
        {
            throw new InvalidOperationException("Actor id must not be empty.");
        }

        await actors.GetOrCreateAsync(
                actorId,
                SampleNames.PlayerActorType,
                cancellationToken)
            .ConfigureAwait(false);

        var route = await actors.GetRouteAsync(
                actorId,
                SampleNames.PlayerActorType,
                cancellationToken)
            .ConfigureAwait(false);
        return new EnsurePlayerActorRes(
            actorId,
            new ActorRouteSnapshot(
                route.RouterChannelId,
                route.TargetNodeRid.ToBytes().ToArray(),
                route.ActorGeneration));
    }
}
