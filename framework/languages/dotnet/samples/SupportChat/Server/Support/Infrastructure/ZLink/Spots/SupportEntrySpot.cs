using SupportChat.Server.Support.Infrastructure.ZLink.Actors;
using SupportChat.Server.Support.Infrastructure.ZLink.Spots.Handlers;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;

namespace SupportChat.Server.Support.Infrastructure.ZLink.Spots;

internal sealed class SupportEntrySpot(
    IZLinkEntrySpotContext context,
    ILogger<SupportEntrySpot> logger) : IZLinkEntrySpot<SupportUserActor>
{
    public IZLinkEntrySpotContext Context { get; } = context;

    public void Configure()
    {
        Context.Handlers.AddHandler<OpenConversationActorHandler>();
        Context.Handlers.AddHandler<SetAgentAvailableHandler>();
    }

    public ValueTask OnCreateActorAsync(
        SupportUserActor actor,
        CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        logger.LogInformation(
            "support entry: actor created. actor={ActorId}, role={Role}",
            actor.ActorId,
            actor.Role);
        return ValueTask.CompletedTask;
    }

    public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        SupportUserActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        _ = actor;
        _ = cancellationToken;
        return ValueTask.FromResult(ZLinkSpotActorJoinResult.Accept(request));
    }

    public ValueTask OnJoinedActorAsync(
        SupportUserActor actor,
        CancellationToken cancellationToken)
    {
        _ = cancellationToken;
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
        _ = cancellationToken;
        logger.LogInformation(
            "support entry: actor left. actor={ActorId}",
            actor.ActorId);
        return ValueTask.CompletedTask;
    }
}
