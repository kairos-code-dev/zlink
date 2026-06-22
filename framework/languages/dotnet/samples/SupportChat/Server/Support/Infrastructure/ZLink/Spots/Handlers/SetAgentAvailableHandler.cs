using SupportChat.Server.Support.Infrastructure.ZLink.Actors;
using SupportChat.Server.Support.Application.ConversationAssignment;
using SupportChat.Server.Configuration;
using SupportChat.Shared.Contracts;
using Zlink.Framework.Contracts.Spots;

namespace SupportChat.Server.Support.Infrastructure.ZLink.Spots.Handlers;

internal sealed class SetAgentAvailableHandler(
    AgentAvailabilityDirectory availability,
    SupportActorDirectory actors)
    : IZLinkEntrySpotActorRequestHandler<SupportEntrySpot, SupportUserActor, SetAgentAvailableReq, SetAgentAvailableRes>
{
    public ValueTask<SetAgentAvailableRes> HandleAsync(
        SupportEntrySpot entrySpot,
        SupportUserActor actor,
        ZLinkSpotActorRequestContext context,
        SetAgentAvailableReq message,
        CancellationToken cancellationToken)
    {
        _ = entrySpot;
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        if (!string.Equals(actor.Role, SupportChatRoles.Agent, StringComparison.Ordinal))
        {
            throw new InvalidOperationException("Only agent actors can set availability.");
        }

        actors.AddOrUpdate(actor);
        availability.SetAvailable(actor.ActorId, actor.DisplayName, message.IsAvailable);
        return ValueTask.FromResult(new SetAgentAvailableRes(message.IsAvailable));
    }
}
