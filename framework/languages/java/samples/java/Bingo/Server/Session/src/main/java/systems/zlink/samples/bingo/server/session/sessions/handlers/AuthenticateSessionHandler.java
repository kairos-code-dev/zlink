package systems.zlink.samples.bingo.server.session.sessions.handlers;


import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.spots.SpotHandle;
import systems.zlink.framework.spots.SpotHandleResolver;
import systems.zlink.framework.locations.ZLinkAllocatedRoutingIdProvider;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;
import systems.zlink.samples.bingo.server.configuration.SampleNames;
import systems.zlink.samples.bingo.server.configuration.SampleTimings;
import systems.zlink.samples.bingo.shared.contracts.BingoMessages;
import systems.zlink.samples.bingo.shared.contracts.Messages;

public final class AuthenticateSessionHandler
    implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, Messages.AuthenticateReq> {
    private final ZLinkRouteClient routes;
    private final SpotHandleResolver spots;
    private final ZLinkAllocatedRoutingIdProvider allocatedRoutingIds;

    public AuthenticateSessionHandler(
        ZLinkRouteClient routes,
        SpotHandleResolver spots,
        ZLinkAllocatedRoutingIdProvider allocatedRoutingIds) {
        this.routes = routes;
        this.spots = spots;
        this.allocatedRoutingIds = allocatedRoutingIds;
    }

    @Override
    public Class<Messages.AuthenticateReq> messageType() {
        return Messages.AuthenticateReq.class;
    }

    @Override
    public java.util.concurrent.CompletionStage<Void> handle(
        ZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        Messages.AuthenticateReq request) {
        if (request.getAccessToken().isBlank()) {
            throw new IllegalArgumentException("access token is required");
        }
        return routes
            .requestToChannel(
                SampleNames.ApiChannel,
                BingoMessages.authenticatePlayerReq(request.getAccessToken()))
            .timeout(SampleTimings.RequestTimeout)
            .submit(Messages.AuthenticatePlayerRes.class)
            .thenCompose(authenticated -> {
                requireAuthenticated(authenticated);
                return allocatedRoutingIds.waitForReadyAllocation("bingo.session")
                    .thenCompose(allocation -> {
                        RoutingId preferredPlayNode = RoutingId.from(
                            "play" + allocation.slot());
                        return spots.resolveSpotHandle(
                            SampleNames.Mesh,
                            preferredPlayNode)
                    .thenCompose(handle -> routes.requestToSpot(
                            requireSpot(handle, preferredPlayNode),
                            BingoMessages.ensurePlayerActorReq(
                                authenticated.getActorId(),
                                authenticated.getDisplayName(),
                                preferredPlayNode.toString()))
                        .timeout(SampleTimings.RequestTimeout)
                        .submit(Messages.EnsurePlayerActorRes.class))
                    .thenCompose(ensured -> context.actors().bind(new ActorRef(
                            RoutingId.from(ensured.getActor().getNodeRid()),
                            ensured.getActor().getActorId(),
                            ensured.getActor().getGeneration()))
                        .thenRun(() -> context.client().reply(BingoMessages.authenticateRes(
                            ensured.getActorId(),
                            authenticated.getDisplayName(),
                            ensured.getActor().getNodeRid())).submit()));
                    });
            });
    }

    private static void requireAuthenticated(Messages.AuthenticatePlayerRes authenticated) {
        if (!authenticated.getAccepted()
            || authenticated.getActorId().isBlank()
            || authenticated.getDisplayName().isBlank()) {
            throw new IllegalStateException(authenticated.getReason().isBlank()
                ? "Player authentication failed."
                : authenticated.getReason());
        }
    }

    private static SpotHandle requireSpot(
        java.util.Optional<SpotHandle> handle,
        RoutingId spotRid) {
        return handle.orElseThrow(() -> new IllegalStateException("spot not found: " + spotRid));
    }
}
