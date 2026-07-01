using Microsoft.Extensions.Logging;
using SupportChat.Server.Configuration;
using SupportChat.Server.Support.Application.ConversationAssignment;
using SupportChat.Server.Support.Infrastructure.ZLink.Actors;
using SupportChat.Server.Support.Infrastructure.ZLink.Spots.EntrySpot.Handlers;
using SupportChat.Shared.Contracts;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;

namespace SupportChat.Server.Support.Infrastructure.ZLink.Spots.EntrySpot;

internal sealed class SupportEntrySpot(
    IZLinkEntrySpotContext context,
    SupportActorDirectory directory,
    AgentAvailabilityDirectory availability,
    IZLinkActorManager actorManager,
    ILogger<SupportEntrySpot> logger) : IZLinkEntrySpot<SupportUserActor>
{
    public IZLinkEntrySpotContext Context { get; } = context;

    public void Configure()
    {
        Context.Handlers.AddHandler<OpenConversationActorHandler>();
        Context.Handlers.AddHandler<SetAgentAvailableHandler>();
    }

    public async ValueTask OnCreateActorAsync(
        SupportUserActor actor,
        ZLinkMessage createRequest,
        CancellationToken cancellationToken)
    {
        var request = createRequest.Decode<EnsureSupportUserActorReq>();
        actor.SetIdentity(request.DisplayName, request.Role, request.ParticipantId);
        var actorRef = await actorManager.FindAsync(actor.ActorId, cancellationToken)
                       ?? throw new InvalidOperationException(
                           $"Support actor ref is not available. actor={actor.ActorId}");
        directory.AddOrUpdate(actor, actorRef);
        logger.LogInformation(
            "support entry: actor created. actor={ActorId}, role={Role}",
            actor.ActorId,
            actor.Role);
    }

    public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        SupportUserActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        return ValueTask.FromResult(ZLinkSpotActorJoinResult.Accept(request));
    }

    public ValueTask OnJoinedActorAsync(
        SupportUserActor actor,
        CancellationToken cancellationToken)
    {
        logger.LogInformation(
            "support entry: actor joined. actor={ActorId}, role={Role}",
            actor.ActorId,
            actor.Role);
        return ValueTask.CompletedTask;
    }

    public ValueTask OnLeaveActorAsync(
        SupportUserActor actor,
        CancellationToken cancellationToken)
    {
        logger.LogInformation(
            "support entry: actor left. actor={ActorId}",
            actor.ActorId);
        return ValueTask.CompletedTask;
    }

    // Fires when a member actor's stream session disconnects. An agent's roster actor
    // leaves the assignable list so it stops receiving new conversations until it
    // reconnects and re-registers availability (§9).
    public ValueTask OnDisconnectActorAsync(
        SupportUserActor actor,
        CancellationToken cancellationToken)
    {
        if (string.Equals(actor.Role, SupportChatRoles.Agent, StringComparison.Ordinal))
        {
            availability.SetAvailable(actor.ActorId, string.Empty, false);
            logger.LogInformation(
                "support entry: agent disconnected, availability removed. actor={ActorId}",
                actor.ActorId);
        }

        return ValueTask.CompletedTask;
    }
}