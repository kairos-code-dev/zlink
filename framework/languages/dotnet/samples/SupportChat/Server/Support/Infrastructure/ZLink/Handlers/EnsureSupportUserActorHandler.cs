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

        var actor = (await actors.GetOrCreate(request.ActorId, SampleNames.SupportActorType)
            .Request(request).Async(cancellationToken)) switch
        {
            ZLinkActorCreateResult.Existing value => value.Actor,
            ZLinkActorCreateResult.Created value => value.Actor,
            _ => throw new InvalidOperationException("Support Actor creation was rejected.")
        };

        return ToResponse(actor);
    }

    private static EnsureSupportUserActorRes ToResponse(ActorRef actor)
    {
        return new EnsureSupportUserActorRes(
            ActorRefSnapshot.From(actor));
    }
}
