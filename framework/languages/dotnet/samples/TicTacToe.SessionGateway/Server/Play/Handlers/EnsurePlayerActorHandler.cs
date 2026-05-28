using TicTacToe.SessionGateway.Shared.Actors;
using TicTacToe.SessionGateway.Shared.Configuration;
using TicTacToe.SessionGateway.Shared.Contracts;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Handlers;

namespace TicTacToe.SessionGateway.Server.Play.Handlers;

[ZLinkHandlerGroup("play")]
internal sealed class EnsurePlayerActorHandler(
    IZLinkActorManager actors,
    SampleTopology topology)
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
            ;

        var joined = await actor.Context.JoinEntrySpot(topology.PlayRid)
            .Timeout(SampleTimings.RequestTimeout)
            .SubmitAsync(cancellationToken)
            ;
        return new EnsurePlayerActorRes(
            request.ActorId,
            SampleNames.PlayerActorType,
            new ActorRefSnapshot(
                joined.NodeRid.ToBytes().ToArray(),
                joined.ActorId,
                joined.Generation));
    }
}
