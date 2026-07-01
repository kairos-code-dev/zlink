package systems.zlink.e2e.spotservice.shared;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.handlers.ZLinkSpotActorSend;
import systems.zlink.framework.spots.ZLinkSpotActorSendContext;

public final class UserActorLeaveHandler {
    @ZLinkSpotActorSend(packetName = "LeaveActorReq")
    public void handle(
        UserSpot spot,
        ScenarioActor actor,
        ZLinkSpotActorSendContext context,
        Contracts.LeaveActorReq request,
        CancellationToken cancellationToken) {
        if (!request.actorId().equals(actor.actorId())) {
            throw new IllegalStateException("leave request actor does not match dispatched actor");
        }
        spot.context().leaveActor(actor).toCompletableFuture().join();
    }
}
