using Microsoft.Extensions.Logging;
using SupportChat.Server.Configuration;
using SupportChat.Server.Support.Infrastructure.ZLink.Actors;
using SupportChat.Shared.Contracts;
using Systems.Zlink;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Handlers;

namespace SupportChat.Server.Support.Infrastructure.ZLink.Handlers;

// Creates (or reuses) the conversation agent actor for one assigned conversation and
// joins it into that ConversationSpot. The actor's ParticipantId is the agent's roster
// id, so the conversation sees a single human agent regardless of which conversation
// actor carries inbound traffic (§9.1). Called by the Session server when the agent
// client joins an assigned conversation.
[ZLinkHandlerGroup("support")]
internal sealed class EnsureAgentConversationHandler(
    IZLinkActorManager actors,
    SupportActorDirectory directory,
    ILogger<EnsureAgentConversationHandler> logger)
    : IZLinkRequestHandler<EnsureAgentConversationReq, EnsureAgentConversationRes>
{
    public async ValueTask<EnsureAgentConversationRes> HandleAsync(
        EnsureAgentConversationReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        var conversationActorId = $"{request.RosterActorId}@{request.ConversationId}";
        var actorRef = await actors.FindAsync(conversationActorId, cancellationToken);
        if (actorRef is null)
        {
            actorRef = await actors
                .GetOrCreate(conversationActorId, SampleNames.SupportActorType)
                .Request(new EnsureSupportUserActorReq(
                    conversationActorId,
                    request.DisplayName,
                    SupportChatRoles.Agent,
                    request.RosterActorId))
                .Async(cancellationToken) switch
            {
                ZLinkActorCreateResult.Existing value => value.Actor,
                ZLinkActorCreateResult.Created value => value.Actor,
                _ => throw new InvalidOperationException("Actor creation was rejected.")
            };
        }

        var conversationActor = directory.Get(conversationActorId);
        var joined = await conversationActor.Actor.Context.JoinSpot(
                RoutingId.From(request.ConversationId),
                new JoinConversationReq(
                    conversationActor.Actor.ParticipantId,
                    conversationActor.Actor.Role,
                    conversationActor.Actor.DisplayName))
            .Async(cancellationToken);
        var reply = joined switch
        {
            ZLinkActorJoinResult.Accepted accepted => accepted.Reply,
            ZLinkActorJoinResult.Rejected rejected => rejected.Reply,
            _ => throw new InvalidOperationException("Unknown actor join result.")
        };
        var state = reply.Decode<JoinConversationRes>().State;

        logger.LogInformation(
            "support agent conversation: joined. conversation={ConversationId}, roster={RosterActorId}",
            request.ConversationId,
            request.RosterActorId);
        return new EnsureAgentConversationRes(
            ActorRefSnapshot.From(actorRef.Value),
            state);
    }
}
