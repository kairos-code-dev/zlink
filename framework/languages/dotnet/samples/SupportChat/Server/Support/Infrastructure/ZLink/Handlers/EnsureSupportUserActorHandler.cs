using Zlink.Framework.Contracts.Codecs.Json;
using Systems.Zlink;
using SupportChat.Server.Support.Infrastructure.ZLink.Actors;
using SupportChat.Server.Configuration;
using SupportChat.Shared.Contracts;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Messaging;

namespace SupportChat.Server.Support.Infrastructure.ZLink.Handlers;

[ZLinkHandlerGroup("support")]
internal sealed class EnsureSupportUserActorHandler(
    IZLinkActorManager actors,
    SampleTopology topology)
    : IZLinkRequestHandler<EnsureSupportUserActorReq, EnsureSupportUserActorRes>
{
    public async ValueTask<EnsureSupportUserActorRes> HandleAsync(
        EnsureSupportUserActorReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        if (await actors.FindAsync(request.ActorId, cancellationToken) is { } existing)
        {
            return ToResponse(existing);
        }

        var joined = await actors.JoinEntrySpotAsync(
                request.ActorId,
                SampleNames.SupportActorType,
                topology.SupportEntryRid,
                request,
            cancellationToken);
        if (!joined.Accepted)
        {
            throw new InvalidOperationException($"Entry spot actor join was rejected: {request.ActorId}");
        }

        return ToResponse(joined.Actor);
    }

    private static EnsureSupportUserActorRes ToResponse(ActorRef actor)
    {
        return new EnsureSupportUserActorRes(
            new ActorRefSnapshot(
                actor.NodeRid.ToBytes().ToArray(),
                actor.ActorId,
                actor.Generation));
    }
}
