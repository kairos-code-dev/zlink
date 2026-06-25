package systems.zlink.e2e.spotservice;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;

public final class ScenarioEntrySpot implements ZLinkEntrySpot<ScenarioActor> {
    private final ZLinkEntrySpotContext context;
    private final ScenarioState evidence;

    public ScenarioEntrySpot(
        ZLinkEntrySpotContext context,
        ScenarioState evidence) {
        this.context = context;
        this.evidence = evidence;
    }

    @Override
    public ZLinkEntrySpotContext context() {
        return context;
    }

    public String nodeRid() {
        return evidence.nodeRid();
    }

    public void record(String marker, String value) {
        evidence.record(marker, "entry", value);
    }

    @Override
    public void configure() {
        context.handlers().addActorRequest(EntryActorEchoHandler.class);
        context.handlers().addActorRequest(EntryActorJoinHandler.class);
    }

    @Override
    public void onCreateActor(
        ScenarioActor actor,
        ZLinkMessage createRequest,
        CancellationToken cancellationToken) {
        if (!createRequest.isEmpty()) {
            Contracts.ActorAuthRequest request = createRequest.decode(Contracts.ActorAuthRequest.class);
            actor.applyProfile(request.profile());
        }
        evidence.record("ActorCreated", "entry", actor.actorId() + "#" + actor.nextSequence());
    }

    @Override
    public ZLinkSpotActorJoinResponse onActorJoin(
        ScenarioActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken) {
        evidence.record("ActorEntryJoinRequested", "entry", actor.actorId() + "#" + actor.nextSequence());
        return ZLinkSpotActorJoinResponse.accept();
    }

    @Override
    public void onJoinedActor(
        ScenarioActor actor,
        CancellationToken cancellationToken) {
        evidence.record("ActorEntryJoined", "entry", actor.actorId() + "#" + actor.nextSequence());
    }
}
