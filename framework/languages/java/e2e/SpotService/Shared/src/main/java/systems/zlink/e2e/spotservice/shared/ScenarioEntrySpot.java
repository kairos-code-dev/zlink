package systems.zlink.e2e.spotservice.shared;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResult;

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
        context.handlers().addHandler(EntryActorPingHandler.class);
        context.handlers().addHandler(EntryActorEchoHandler.class);
        context.handlers().addHandler(EntryActorPushHandler.class);
        context.handlers().addHandler(EntryActorSnapshotHandler.class);
        context.handlers().addHandler(EntryActorJoinHandler.class);
        context.handlers().addHandler(EntryActorJoinAdmissionHandler.class);
        context.handlers().addHandler(EntrySpotOnlyJoinHandler.class);
        context.handlers().addHandler(EntryActorDestroyHandler.class);
    }

    @Override
    public CompletionStage<Void> onCreateActor(
        ScenarioActor actor,
        ZLinkMessage createRequest) {
        if (!createRequest.isEmpty()) {
            Contracts.ActorAuthReq request = createRequest.decode(Contracts.ActorAuthReq.class);
            actor.applyProfile(request.profile());
            evidence.record("ActorCreatedPayload", "entry",
                request.profile().displayName() + "/"
                    + request.profile().level() + "/"
                    + String.join(",", request.profile().tags()));
        }
        evidence.record("ActorCreated", "entry", actor.actorId() + "#" + actor.nextSequence());
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public CompletionStage<ZLinkSpotActorJoinResult> onActorJoin(
        String actorId,
        ZLinkMessage request) {
        evidence.record("ActorEntryJoinRequested", "entry", actorId);
        return CompletableFuture.completedFuture(ZLinkSpotActorJoinResult.accept());
    }

    @Override
    public CompletionStage<Void> onJoinedActor(ScenarioActor actor) {
        evidence.record("ActorEntryJoined", "entry", actor.actorId() + "#" + actor.nextSequence());
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public CompletionStage<Void> onLeaveActor(ScenarioActor actor) {
        return CompletableFuture.completedFuture(null);
    }
}
