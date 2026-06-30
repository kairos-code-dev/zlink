using SupportChat.Server.Configuration;
using SupportChat.Server.Support.Infrastructure.ZLink.Actors;
using SupportChat.Server.Support.Infrastructure.ZLink.Spots.ConversationSpot.Notifications;
using SupportChat.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Systems.Zlink;
using Zlink.Framework.Contracts.Spots;

namespace SupportChat.Server.Support.Infrastructure.ZLink.Spots.EntrySpot.Handlers;

internal sealed class OpenConversationActorHandler(
    SupportActorDirectory actors,
    ConversationNotificationPublisher notifications,
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
        var opened = await entrySpot.Context.Outbound.RequestToChannel(
                SampleNames.ApiChannel,
                new OpenConversationApiReq(
                    actor.ActorId,
                    actor.DisplayName,
                    message.Subject))
            .Async<OpenConversationApiRes>(cancellationToken);
        var conversationRid = RoutingId.From(opened.ConversationId);
        if (opened.AgentActorId is not null)
        {
            var agent = actors.Get(opened.AgentActorId);
            var agentJoined = await agent.Actor.Context.JoinSpot(
                    conversationRid,
                    new JoinConversationReq(opened.ConversationId))
                .Async(cancellationToken);
            var agentState = agentJoined.Reply.Decode<JoinConversationRes>().State;
            await notifications.PublishAssignedToAgentAsync(
                agent.Actor,
                agentState,
                cancellationToken);
        }

        var joined = await actor.Context.JoinSpot(
                conversationRid,
                new JoinConversationReq(opened.ConversationId))
            .Async(cancellationToken);
        var state = joined.Reply.Decode<JoinConversationRes>().State;
        await notifications.PublishJoinedAgentToCustomerAsync(
            actor,
            state,
            cancellationToken);
        logger.LogInformation(
            "support entry open: completed conversation={ConversationId} status={Status}",
            opened.ConversationId,
            state.Status);
        return new OpenConversationRes(opened.ConversationId, state);
    }
}
