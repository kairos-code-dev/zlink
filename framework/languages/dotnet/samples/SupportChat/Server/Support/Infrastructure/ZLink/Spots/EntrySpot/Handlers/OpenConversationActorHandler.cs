using Systems.Zlink;
using SupportChat.Server.Support.Infrastructure.ZLink.Actors;
using SupportChat.Server.Support.Infrastructure.ZLink.Spots.ConversationSpot.Notifications;
using SupportChat.Server.Configuration;
using SupportChat.Shared.Contracts;
using Zlink.Framework.Contracts.Spots;

namespace SupportChat.Server.Support.Infrastructure.ZLink.Spots.EntrySpot.Handlers;

internal sealed class OpenConversationActorHandler(
    SupportActorDirectory actors,
    ConversationNotificationPublisher notifications)
    : IZLinkEntrySpotActorRequestHandler<SupportEntrySpot, SupportUserActor, OpenConversationReq, OpenConversationRes>
{
    public async ValueTask<OpenConversationRes> HandleAsync(
        SupportEntrySpot entrySpot,
        SupportUserActor actor,
        ZLinkSpotActorRequestContext context,
        OpenConversationReq message,
        CancellationToken cancellationToken)
    {
        _ = context;
        if (!string.Equals(actor.Role, SupportChatRoles.Customer, StringComparison.Ordinal))
        {
            throw new InvalidOperationException("Only customer actors can open a conversation.");
        }

        Console.Error.WriteLine($"support entry open: request actor={actor.ActorId} subject={message.Subject}");
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
        Console.Error.WriteLine($"support entry open: completed conversation={opened.ConversationId} status={state.Status}");
        return new OpenConversationRes(opened.ConversationId, state);
    }
}
