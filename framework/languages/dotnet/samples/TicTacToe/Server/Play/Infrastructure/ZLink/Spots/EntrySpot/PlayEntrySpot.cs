using TicTacToe.Server.Play.Infrastructure.ZLink.Actors;
using TicTacToe.Server.Play.Infrastructure.ZLink.Spots.EntrySpot.Handlers;
using TicTacToe.Server.Configuration;
using TicTacToe.Shared.Contracts;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;

namespace TicTacToe.Server.Play.Infrastructure.ZLink.Spots.EntrySpot;

internal sealed class PlayEntrySpot(
    IZLinkEntrySpotContext context,
    ILogger<PlayEntrySpot> logger) : IZLinkEntrySpot<PlayActor>
{
    private readonly MilestoneObserverRegistry _milestoneObservers = new();

    public IZLinkEntrySpotContext Context { get; } = context;

    public void Configure()
    {
        Context.Handlers.AddActorPacket<PlayActorJoinGameHandler, PlayActor>();
        Context.Handlers.AddActorPacket<PlayActorObserveMilestoneHandler, PlayActor>();
        Context.Handlers.AddSubscribe<PlayerWinMilestoneEventHandler>(SampleTopics.PlayerMilestone);
    }

    public ValueTask OnCreateActorAsync(
        PlayActor actor,
        ZLinkMessage createRequest,
        CancellationToken cancellationToken)
    {
        actor.ApplyPlayer(createRequest.Decode<PlayerInfo>());
        logger.LogInformation(
            "entry spot: actor created. actor={ActorId}",
            actor.ActorId);
        return ValueTask.CompletedTask;
    }

    public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        string actorId,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        return ValueTask.FromResult(ZLinkSpotActorJoinResult.Accept(request));
    }

    public async ValueTask OnJoinedActorAsync(
        PlayActor actor,
        CancellationToken cancellationToken)
    {
        logger.LogInformation(
            "entry spot: actor joined. actor={ActorId}",
            actor.ActorId);
        if (!actor.DestroyAfterEntrySpotJoin) return;

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
        actor.MarkDisconnected();
        _milestoneObservers.Remove(actor);
        logger.LogInformation(
            "entry spot: actor disconnected. actor={ActorId}",
            actor.ActorId);
        return ValueTask.CompletedTask;
    }

    public ValueTask SubscribeMilestoneAsync(
        PlayActor actor,
        CancellationToken cancellationToken)
    {
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

        public ValueTask NotifyAsync(
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
                observer.Context.BoundSession.Send(notify)
                    .Submit(cancellationToken);

            return ValueTask.CompletedTask;
        }
    }
}
