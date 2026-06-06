using Microsoft.Extensions.Logging;
using TicTacToe.SessionGateway.Play.EntrySpot.Handlers;
using TicTacToe.SessionGateway.Shared.Actors;
using Zlink.Framework.Contracts.Spots;

namespace TicTacToe.SessionGateway.Server.Play.EntrySpot;

internal sealed class TicTacToeEntrySpot(
    IZLinkEntrySpotContext context,
    ILogger<TicTacToeEntrySpot> logger) : IZLinkEntrySpot<PlayerActor>
{
    public IZLinkEntrySpotContext Context { get; } = context;

    public void Configure()
    {
        Context.Handlers.AddHandler<JoinMatchHandler>();
    }

    public ValueTask OnPostActorJoinedAsync(
        PlayerActor actor,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        logger.LogInformation(
            "entry spot: actor joined. actor={ActorId}",
            actor.ActorId);
        return ValueTask.CompletedTask;
    }

    public ValueTask OnActorLeftAsync(
        PlayerActor actor,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        logger.LogInformation(
            "entry spot: actor left. actor={ActorId}",
            actor.ActorId);
        return ValueTask.CompletedTask;
    }
}
