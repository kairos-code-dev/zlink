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
        var actorRef = await actors.FindAsync(conversationActorId, cancellationToken)
                       ?? await actors.GetOrCreateAsync(
                           conversationActorId,
                           SampleNames.SupportActorType,
                           new EnsureSupportUserActorReq(
                               conversationActorId,
                               request.DisplayName,
                               SupportChatRoles.Agent,
                               request.RosterActorId),
                           cancellationToken);

        var conversationActor = directory.Get(conversationActorId);
        var joined = await conversationActor.Actor.Context.JoinSpot(
                RoutingId.From(request.ConversationId),
                new JoinConversationReq())
            .Async(cancellationToken);
        var state = joined.Reply.Decode<JoinConversationRes>().State;

        logger.LogInformation(
            "support agent conversation: joined. conversation={ConversationId}, roster={RosterActorId}",
            request.ConversationId,
            request.RosterActorId);
        return new EnsureAgentConversationRes(
            new ActorRefSnapshot(
                actorRef.NodeRid.ToBytes().ToArray(),
                actorRef.ActorId,
                actorRef.Generation),
            state);
    }
}
