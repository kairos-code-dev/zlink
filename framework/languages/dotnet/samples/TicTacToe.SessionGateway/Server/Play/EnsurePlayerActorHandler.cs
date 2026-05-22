using TicTacToe.SessionGateway.Shared.Configuration;
using TicTacToe.SessionGateway.Shared.Contracts;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Handlers;

namespace TicTacToe.SessionActorDispatch.Play;

[ZLinkHandlerGroup("play")]
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
            ;

        var remoteAddress = await actors.GetRemoteAddressAsync(
                actorId,
                SampleNames.PlayerActorType,
                cancellationToken)
            ;
        return new EnsurePlayerActorRes(
            actorId,
            SampleNames.PlayerActorType,
            new ActorRemoteAddressSnapshot(
                remoteAddress.RouterChannelId,
                remoteAddress.TargetNodeRid.ToBytes().ToArray(),
                remoteAddress.ActorGeneration));
    }
}
