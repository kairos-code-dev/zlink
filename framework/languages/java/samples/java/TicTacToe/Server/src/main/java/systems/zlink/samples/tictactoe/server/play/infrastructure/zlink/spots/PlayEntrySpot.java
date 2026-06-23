package systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.spots;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;
import systems.zlink.samples.tictactoe.server.configuration.SampleNames;
import systems.zlink.samples.tictactoe.server.configuration.SampleSettings;
import systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.actors.PlayActor;
import systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.spots.handlers.PlayerWinMilestoneEventHandler;
import systems.zlink.samples.tictactoe.shared.contracts.ObserveMilestoneRes;
import systems.zlink.samples.tictactoe.shared.contracts.PlayerWinMilestoneEvent;
import systems.zlink.samples.tictactoe.shared.contracts.WinMilestoneNotify;

public final class PlayEntrySpot implements ZLinkEntrySpot<PlayActor> {
    private final ZLinkEntrySpotContext context;
    private final SampleSettings settings;
    private final java.util.List<PlayActor> milestoneObservers = new java.util.ArrayList<>();

    public PlayEntrySpot(
        ZLinkEntrySpotContext context,
        SampleSettings settings) {
        this.context = context;
        this.settings = settings;
    }

    @Override
    public ZLinkEntrySpotContext context() {
        return context;
    }

    @Override
    public void configure() {
        context.handlers().addSubscribe(
            SampleNames.PlayerMilestoneTopic,
            PlayerWinMilestoneEventHandler.class);
    }

    @Override
    public void onCreateActor(
        PlayActor actor,
        CancellationToken cancellationToken) {
    }

    @Override
    public ZLinkSpotActorJoinResponse onActorJoin(
        PlayActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken) {
        return ZLinkSpotActorJoinResponse.accept();
    }

    @Override
    public void onJoinedActor(
        PlayActor actor,
        CancellationToken cancellationToken) {
        if (actor.destroyAfterEntrySpotJoin()) {
            await(context.destroyActor(actor));
        }
    }

    @Override
    public void onDisconnectActor(
        PlayActor actor,
        CancellationToken cancellationToken) {
        actor.markDisconnected();
        milestoneObservers.removeIf(existing -> existing.actorId().equals(actor.actorId()));
    }

    public ObserveMilestoneRes observeMilestone(PlayActor actor) {
        rememberObserver(actor);
        return new ObserveMilestoneRes(true);
    }

    public void notifyMilestone(PlayerWinMilestoneEvent event) {
        WinMilestoneNotify payload = new WinMilestoneNotify(
            event.roomId(),
            event.actorId(),
            event.displayName(),
            event.wins(),
            settings.playSpotNodeRid());
        for (PlayActor observer : java.util.List.copyOf(milestoneObservers)) {
            observer.context().boundSession()
                .send(payload)
                .await();
        }
    }

    private void rememberObserver(PlayActor actor) {
        milestoneObservers.removeIf(existing -> existing.actorId().equals(actor.actorId()));
        milestoneObservers.add(actor);
    }
}
