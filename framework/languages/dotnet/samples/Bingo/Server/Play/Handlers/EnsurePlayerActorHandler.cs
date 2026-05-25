using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Timers;
using Bingo.Server.Play.Actors;
using Bingo.Shared.Configuration;
using Bingo.Shared.Contracts;

namespace Bingo.Server.Play.Handlers;

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
            ;
        if (actor is PlayerActor player)
        {
            player.SetDisplayName(request.DisplayName);
        }

        var remoteAddress = await actors.GetRemoteAddressAsync(
                request.ActorId,
                SampleNames.PlayerActorType,
                cancellationToken)
            ;
        return new EnsurePlayerActorRes(
            request.ActorId,
            SampleNames.PlayerActorType,
            new ActorRemoteAddressSnapshot(
                remoteAddress.RouterChannelId,
                remoteAddress.TargetNodeRid.ToBytes().ToArray(),
                remoteAddress.ActorGeneration));
    }
}
