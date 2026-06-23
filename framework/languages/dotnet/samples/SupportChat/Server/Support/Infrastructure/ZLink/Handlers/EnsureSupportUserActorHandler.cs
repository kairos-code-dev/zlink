using Systems.Zlink;
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
    SupportActorDirectory directory,
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
            cancellationToken);
        if (actor is not SupportUserActor supportActor)
        {
            throw new InvalidOperationException("Support actor factory returned an unexpected actor type.");
        }

        supportActor.SetIdentity(request.DisplayName, request.Role);
        directory.AddOrUpdate(supportActor);

        var joined = await actor.Context.JoinEntrySpot(
                topology.SupportEntryRid,
                ZLinkMessage.Empty)
            .Async(cancellationToken);
        if (!string.IsNullOrWhiteSpace(supportActor.ConversationId))
        {
            var conversationRid = RoutingId.From(supportActor.ConversationId);
            await actor.Context.JoinSpot(
                    conversationRid,
                    new JoinConversationReq(supportActor.ConversationId))
                .Async(cancellationToken);
        }

        return new EnsureSupportUserActorRes(
            new ActorRefSnapshot(
                joined.Actor.NodeRid.ToBytes().ToArray(),
                joined.Actor.ActorId,
                joined.Actor.Generation));
    }
}
