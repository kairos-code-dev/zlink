using Microsoft.Extensions.Logging;
using SupportChat.Server.Configuration;
using SupportChat.Server.Support.Infrastructure.ZLink.Actors;
using SupportChat.Shared.Contracts;
using Systems.Zlink;
using Zlink.Framework.Contracts.Spots;

namespace SupportChat.Server.Support.Infrastructure.ZLink.Spots.EntrySpot.Handlers;

internal sealed class OpenConversationActorHandler(
    ILogger<OpenConversationActorHandler> logger)
    : IZLinkEntrySpotActorRequestHandler<SupportEntrySpot, SupportUserActor, OpenConversationReq, OpenConversationRes>
{
    public async ValueTask<OpenConversationRes> HandleAsync(
        SupportEntrySpot entrySpot,
        SupportUserActor actor,
        ZLinkSpotActorRequestContext context,
        OpenConversationReq message,
        CancellationToken cancellationToken)
    {
        if (!string.Equals(actor.Role, SupportChatRoles.Customer, StringComparison.Ordinal))
            throw new InvalidOperationException("Only customer actors can open a conversation.");

        logger.LogInformation(
            "support entry open: request actor={ActorId} subject={Subject}",
            actor.ActorId,
            message.Subject);

        // The API server allocates the conversation; this handler then joins the
        // customer. Agent assignment happens inside the ConversationSpot when the
        // customer joins, so it can reserve and later release the agent's capacity.
        var opened = await entrySpot.Context.Outbound.RequestToChannel(
                SampleNames.ApiChannel,
                new OpenConversationApiReq(
                    actor.ParticipantId,
                    actor.DisplayName,
                    message.Subject))
            .Async<OpenConversationApiRes>(cancellationToken);

        var joined = await actor.Context.JoinSpot(
                RoutingId.From(opened.ConversationId),
                new JoinConversationReq(actor.ParticipantId, actor.Role, actor.DisplayName))
            .Async(cancellationToken);
        var state = joined.Reply.Decode<JoinConversationRes>().State;

        logger.LogInformation(
            "support entry open: completed conversation={ConversationId} status={Status}",
            opened.ConversationId,
            state.Status);
        return new OpenConversationRes(opened.ConversationId, state);
    }
}
