using Systems.Zlink;
using Zlink.Framework.Contracts.Spots;
using TicTacToe.Server.Configuration;
using TicTacToe.Server.Play.Infrastructure.ZLink.Actors;
using TicTacToe.Server.Play.Infrastructure.ZLink.Spots.Handlers;
using TicTacToe.Shared.Contracts;

namespace TicTacToe.Server.Play.Infrastructure.ZLink.Spots;

internal sealed class PlayEntrySpot(
    IZLinkEntrySpotContext context,
    ILogger<PlayEntrySpot> logger) : IZLinkEntrySpot<PlayActor>
{
    private readonly MilestoneObserverRegistry _milestoneObservers = new();

    public IZLinkEntrySpotContext Context { get; } = context;

    public void Configure()
    {
        Context.Handlers.AddActorRequest<PlayActorJoinGameHandler, PlayActor>(nameof(JoinGameReq));
        Context.Handlers.AddActorRequest<PlayActorObserveMilestoneHandler, PlayActor>(nameof(ObserveMilestoneReq));
        Context.Handlers.AddSubscribe<PlayerWinMilestoneEventHandler>(SampleTopics.PlayerMilestone);
    }

    public ValueTask SubscribeMilestoneAsync(
        PlayActor actor,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        _milestoneObservers.Subscribe(actor);
        logger.LogInformation(
            "entry spot: milestone observer subscribed. actor={ActorId}, nodeRid={NodeRid}",
            actor.ActorId,
            Context.NodeRid);
        return ValueTask.CompletedTask;
    }

    public async ValueTask NotifyMilestoneAsync(
        PlayerWinMilestoneEvent milestone,
        CancellationToken cancellationToken)
    {
        await _milestoneObservers.NotifyAsync(
            milestone,
            Context.NodeRid.ToString(),
            cancellationToken);
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
        _milestoneObservers.Remove(actor);
        return ValueTask.CompletedTask;
    }

    public ValueTask OnDisconnectActorAsync(
        PlayActor actor,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        actor.MarkDisconnected();
        _milestoneObservers.Remove(actor);
        logger.LogInformation(
            "entry spot: actor disconnected. actor={ActorId}",
            actor.ActorId);
        return ValueTask.CompletedTask;
    }

    private sealed class MilestoneObserverRegistry
    {
        private readonly Dictionary<string, PlayActor> _observers = new(StringComparer.Ordinal);

        public void Subscribe(PlayActor actor)
        {
            _observers[actor.ActorId] = actor;
        }

        public void Remove(PlayActor actor)
        {
            _observers.Remove(actor.ActorId);
        }

        public async ValueTask NotifyAsync(
            PlayerWinMilestoneEvent milestone,
            string receivingSpotNodeRid,
            CancellationToken cancellationToken)
        {
            var notify = new WinMilestoneNotify(
                milestone.RoomId,
                milestone.ActorId,
                milestone.DisplayName,
                milestone.Wins,
                receivingSpotNodeRid);

            var observers = _observers.Values.ToArray();
            foreach (var observer in observers)
            {
                await observer.Context.BoundSession.Send(notify)
                    .Async(cancellationToken);
            }
        }
    }
}
