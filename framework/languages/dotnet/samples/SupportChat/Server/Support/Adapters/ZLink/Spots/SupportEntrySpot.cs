using SupportChat.Server.Support.Adapters.ZLink.Actors;
using SupportChat.Server.Support.Adapters.ZLink.Spots.Handlers;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Spots;

namespace SupportChat.Server.Support.Adapters.ZLink.Spots;

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

    public ValueTask onCreateActor(
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

    public ValueTask onJoinActor(
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

    public ValueTask onLeaveActor(
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
