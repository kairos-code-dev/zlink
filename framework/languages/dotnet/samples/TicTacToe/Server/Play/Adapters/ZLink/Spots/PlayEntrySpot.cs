using Systems.Zlink;
using Zlink.Framework.Contracts.Spots;
using TicTacToe.Server.Play.Adapters.ZLink.Actors;
using TicTacToe.Server.Play.Adapters.ZLink.Spots.Handlers;
using TicTacToe.Shared.Contracts;

namespace TicTacToe.Server.Play.Adapters.ZLink.Spots;

internal sealed class PlayEntrySpot(
    IZLinkEntrySpotContext context,
    ILogger<PlayEntrySpot> logger) : IZLinkEntrySpot<PlayActor>
{
    public IZLinkEntrySpotContext Context { get; } = context;

    public void Configure()
    {
        Context.Handlers.AddActorRequest<PlayActorJoinGameHandler, PlayActor>(nameof(JoinGameReq));
    }

    public ValueTask OnCreateActorAsync(
        PlayActor actor,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        logger.LogInformation(
            "entry spot: actor created. actor={ActorId}",
            actor.ActorId);
        return ValueTask.CompletedTask;
    }

    public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        PlayActor actor,
        Message request,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        _ = actor;
        return ValueTask.FromResult(ZLinkSpotActorJoinResult.Accept(Message.From(request)));
    }

    public async ValueTask OnJoinedActorAsync(
        PlayActor actor,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
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

    public ValueTask OnLeaveActorAsync(
        PlayActor actor,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        logger.LogInformation(
            "entry spot: actor left. actor={ActorId}",
            actor.ActorId);
        return ValueTask.CompletedTask;
    }

    public ValueTask OnDisconnectActorAsync(
        PlayActor actor,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        actor.MarkDisconnected();
        logger.LogInformation(
            "entry spot: actor disconnected. actor={ActorId}",
            actor.ActorId);
        return ValueTask.CompletedTask;
    }
}
