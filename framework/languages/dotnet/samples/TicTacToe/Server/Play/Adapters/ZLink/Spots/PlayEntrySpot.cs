using Zlink.Framework.Contracts.Spots;
using TicTacToe.Server.Play.Adapters.ZLink.Actors;
using TicTacToe.Server.Play.Adapters.ZLink.Spots.Handlers;

namespace TicTacToe.Server.Play.Adapters.ZLink.Spots;

internal sealed class PlayEntrySpot(
    IZLinkEntrySpotContext context,
    ILogger<PlayEntrySpot> logger) : IZLinkEntrySpot<PlayActor>
{
    public IZLinkEntrySpotContext Context { get; } = context;

    public void Configure()
    {
        Context.Handlers.AddHandler<PlayActorJoinGameHandler>();
    }

    public ValueTask OnPostActorJoinedAsync(
        PlayActor actor,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        logger.LogInformation(
            "entry spot: actor joined. actor={ActorId}",
            actor.ActorId);
        return ValueTask.CompletedTask;
    }

    public ValueTask OnActorLeftAsync(
        PlayActor actor,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        logger.LogInformation(
            "entry spot: actor left. actor={ActorId}",
            actor.ActorId);
        return ValueTask.CompletedTask;
    }
}
