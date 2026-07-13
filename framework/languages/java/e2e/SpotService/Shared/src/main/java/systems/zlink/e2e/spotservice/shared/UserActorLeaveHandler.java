package systems.zlink.e2e.spotservice.shared;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.handlers.ZLinkSpotActorSend;
import systems.zlink.framework.spots.ZLinkSpotActorSendContext;

public final class UserActorLeaveHandler {
    @ZLinkSpotActorSend(packetName = "LeaveActorReq")
    public CompletionStage<Void> handle(
        UserSpot spot,
        ScenarioActor actor,
        ZLinkSpotActorSendContext context,
        Contracts.LeaveActorReq request) {
        if (!request.actorId().equals(actor.actorId())) {
            throw new IllegalStateException("leave request actor does not match dispatched actor");
        }
        return spot.context().leaveActor(actor);
    }
}
