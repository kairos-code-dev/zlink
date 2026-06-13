using Zlink.Framework.Contracts.Spots;
using Bingo.Server.Play.Adapters.ZLink.Actors;
using Bingo.Server.Play.Adapters.ZLink.Spots.Handlers;
using Microsoft.Extensions.Logging;

namespace Bingo.Server.Play.Adapters.ZLink.Spots;

internal sealed class BingoEntrySpot(
    IZLinkEntrySpotContext context,
    ILogger<BingoEntrySpot> logger) : IZLinkEntrySpot<PlayerActor>
{
    public IZLinkEntrySpotContext Context { get; } = context;

    public void Configure()
    {
        Context.Handlers.AddHandler<MatchBingoActorHandler>();
    }

    public ValueTask onCreateActor(
        PlayerActor actor,
        CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        logger.LogInformation(
            "entry spot: actor created. actor={ActorId}",
            actor.ActorId);
        return ValueTask.CompletedTask;
    }

    public async ValueTask onJoinActor(
        PlayerActor actor,
        CancellationToken cancellationToken)
    {
        logger.LogInformation(
            "entry spot: actor joined. actor={ActorId}",
            actor.ActorId);
        if (!actor.DestroyAfterEntrySpotJoin)
        {
            return;
        }

        logger.LogInformation(
            "entry spot: actor destroy requested. actor={ActorId}",
            actor.ActorId);
        await Context.DestroyActorAsync(actor, cancellationToken);
        logger.LogInformation(
            "entry spot: actor destroy completed. actor={ActorId}",
            actor.ActorId);
    }

    public ValueTask onLeaveActor(
        PlayerActor actor,
        CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        logger.LogInformation(
            "entry spot: actor left. actor={ActorId}",
            actor.ActorId);
        return ValueTask.CompletedTask;
    }

    public ValueTask onDisconnectActor(
        PlayerActor actor,
        CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        actor.MarkDisconnected();
        logger.LogInformation(
            "entry spot: actor disconnected. actor={ActorId}",
            actor.ActorId);
        return ValueTask.CompletedTask;
    }
}
