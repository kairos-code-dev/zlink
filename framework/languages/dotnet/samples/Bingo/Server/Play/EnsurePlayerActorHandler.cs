using Bingo.Shared.Configuration;
using Bingo.Shared.Contracts;

namespace Bingo.Server.Play;

internal sealed class EnsurePlayerActorHandler(IZLinkActorManager actors)
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

        return new EnsurePlayerActorRes(request.ActorId);
    }
}
