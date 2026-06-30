using SupportChat.Server.Configuration;
using SupportChat.Server.Support.Application.ConversationAssignment;
using SupportChat.Server.Support.Infrastructure.ZLink.Actors;
using SupportChat.Shared.Contracts;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Spots;

namespace SupportChat.Server.Support.Infrastructure.ZLink.Spots.EntrySpot.Handlers;

internal sealed class SetAgentAvailableHandler(
    AgentAvailabilityDirectory availability,
    IZLinkActorManager actorManager,
    SupportActorDirectory actors)
    : IZLinkEntrySpotActorRequestHandler<SupportEntrySpot, SupportUserActor, SetAgentAvailableReq, SetAgentAvailableRes>
{
    public async ValueTask<SetAgentAvailableRes> HandleAsync(
        SupportEntrySpot entrySpot,
        SupportUserActor actor,
        ZLinkSpotActorRequestContext context,
        SetAgentAvailableReq message,
        CancellationToken cancellationToken)
    {
        if (!string.Equals(actor.Role, SupportChatRoles.Agent, StringComparison.Ordinal))
            throw new InvalidOperationException("Only agent actors can set availability.");

        var actorRef = await actorManager.FindAsync(actor.ActorId, cancellationToken)
                       ?? throw new InvalidOperationException(
                           $"Support actor ref is not available. actor={actor.ActorId}");
        actors.AddOrUpdate(actor, actorRef);
        availability.SetAvailable(actor.ActorId, actor.DisplayName, message.IsAvailable);
        return new SetAgentAvailableRes(message.IsAvailable);
    }
}
