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

    public ValueTask OnPostActorJoinedAsync(
        PlayerActor actor,
        CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        logger.LogInformation(
            "entry spot: actor joined. actor={ActorId}",
            actor.ActorId);
        return ValueTask.CompletedTask;
    }

    public ValueTask OnActorLeftAsync(
        PlayerActor actor,
        CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        logger.LogInformation(
            "entry spot: actor left. actor={ActorId}",
            actor.ActorId);
        return ValueTask.CompletedTask;
    }
}
