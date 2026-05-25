using Systems.Zlink;
using TicTacToe.SessionGateway.Shared.Actors;
using TicTacToe.SessionGateway.Shared.Configuration;
using TicTacToe.SessionGateway.Shared.Contracts;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Handlers;

namespace TicTacToe.SessionGateway.Server.Play.Handlers;

[ZLinkHandlerGroup("play")]
internal sealed class JoinEntrySpotActorHandler(IZLinkActorManager actors)
{
    [ZLinkRequest]
    public async ValueTask<JoinEntrySpotActorRes> JoinEntrySpotActor(
        JoinEntrySpotActorReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        var actorId = request.ActorId.Trim();
        if (string.IsNullOrWhiteSpace(actorId))
        {
            throw new InvalidOperationException("Actor id must not be empty.");
        }

        _ = (PlayerActor)await actors.GetOrCreateAsync(
                actorId,
                SampleNames.PlayerActorType,
                cancellationToken)
            ;

        var remoteAddress = await actors.GetRemoteAddressAsync(
                actorId,
                SampleNames.PlayerActorType,
                cancellationToken)
            ;

        return new JoinEntrySpotActorRes(new ActorRefSnapshot(
            actorId,
            SampleNames.PlayerActorType,
            remoteAddress.RouterChannelId,
            remoteAddress.TargetNodeRid.ToBytes().ToArray(),
            remoteAddress.ActorGeneration));
    }
}
