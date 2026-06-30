using Bingo.Server.Configuration;
using Bingo.Shared.Contracts;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Handlers;

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
        var actor = await actors.GetOrCreateAsync(
            request.ActorId,
            SampleNames.PlayerActorType,
            request,
            cancellationToken);

        return new EnsurePlayerActorRes
        {
            ActorId = request.ActorId,
            ActorType = SampleNames.PlayerActorType,
            Actor = new ActorRefSnapshot
            {
                NodeRid = actor.NodeRid.ToString(),
                ActorId = actor.ActorId,
                Generation = actor.Generation
            }
        };
    }
}