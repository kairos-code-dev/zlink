using SupportChat.Server.Configuration;
using SupportChat.Shared.Contracts;
using Systems.Zlink;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Handlers;

namespace SupportChat.Server.Support.Infrastructure.ZLink.Handlers;

[ZLinkHandlerGroup("support")]
internal sealed class EnsureSupportUserActorHandler(
    IZLinkActorManager actors)
    : IZLinkRequestHandler<EnsureSupportUserActorReq, EnsureSupportUserActorRes>
{
    public async ValueTask<EnsureSupportUserActorRes> HandleAsync(
        EnsureSupportUserActorReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        if (await actors.FindAsync(request.ActorId, cancellationToken) is { } existing) return ToResponse(existing);

        var actor = await actors.GetOrCreateAsync(
            request.ActorId,
            SampleNames.SupportActorType,
            request,
            cancellationToken);

        return ToResponse(actor);
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