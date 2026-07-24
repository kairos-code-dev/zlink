using Bingo.Server.Configuration;
using Bingo.Server.Play.Infrastructure.ZLink.Spots.EntrySpot;
using Bingo.Shared.Contracts;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;

namespace Bingo.Server.Play.Infrastructure.ZLink.Handlers;

[ZLinkHandlerGroup("play")]
internal sealed class EnsurePlayerActorHandler(
    IZLinkActorManager actors)
    : IZLinkSpotRequestHandler<BingoEntrySpot, EnsurePlayerActorReq, EnsurePlayerActorRes>
{
    public async ValueTask<EnsurePlayerActorRes> HandleAsync(
        BingoEntrySpot spot,
        EnsurePlayerActorReq request,
        CancellationToken cancellationToken)
    {
        var actor = (await actors.GetOrCreate(request.ActorId, SampleNames.PlayerActorType)
            .Request(request).Async(cancellationToken)) switch
        {
            ZLinkActorCreateResult.Existing value => value.Actor,
            ZLinkActorCreateResult.Created value => value.Actor,
            _ => throw new InvalidOperationException("Player Actor creation was rejected.")
        };

        return new EnsurePlayerActorRes
        {
            ActorId = request.ActorId,
            ActorType = SampleNames.PlayerActorType,
            Actor = new ActorRefWire
            {
                NodeRid = actor.NodeRid.ToString(),
                ActorId = actor.ActorId,
                Generation = actor.Generation
            }
        };
    }
}
