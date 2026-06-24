using Zlink.Framework.Contracts.Codecs.Json;
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
        var actor = await actors.GetOrCreateAsync(
            request.ActorId,
            SampleNames.SupportActorType,
            request,
            cancellationToken);

        _ = topology;

        return new EnsureSupportUserActorRes(
            new ActorRefSnapshot(
                actor.NodeRid.ToBytes().ToArray(),
                actor.ActorId,
                actor.Generation));
    }
}
