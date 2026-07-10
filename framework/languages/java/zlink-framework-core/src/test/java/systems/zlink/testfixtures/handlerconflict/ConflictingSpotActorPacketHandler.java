package systems.zlink.testfixtures.handlerconflict;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.handlers.ZLinkSpotActorSend;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.framework.spots.ZLinkSpotContext;

public final class ConflictingSpotActorPacketHandler {
    @ZLinkSpotActorSend(packetName = "SpotActorSend")
    @ZLinkSpotActorRequest(packetName = "SpotActorRequest")
    public SpotActorReply handle(
        TestSpot spot,
        TestActor actor,
        ZLinkSpotActorRequestContext context,
        SpotActorRequest request,
        CancellationToken cancellationToken) {
        return new SpotActorReply();
    }

    public static final class TestSpot implements ZLinkSpot<ZLinkActor> {
        @Override
        public ZLinkSpotContext context() {
            throw new UnsupportedOperationException();
        }

        @Override public void onJoinedActor(ZLinkActor actor, CancellationToken cancellationToken) { }
        @Override public void onLeaveActor(ZLinkActor actor, CancellationToken cancellationToken) { }
    }

    public static final class TestActor implements ZLinkActor {
        @Override
        public String actorId() {
            return "actor";
        }

        @Override
        public ZLinkActorContext context() {
            throw new UnsupportedOperationException();
        }
    }

    public record SpotActorRequest() {
    }

    public record SpotActorReply() {
    }
}
