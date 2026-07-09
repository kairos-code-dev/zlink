package systems.zlink.samples.bingo.server.session.sessions.handlers;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.locations.SpotRef;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;
import systems.zlink.samples.bingo.server.configuration.SampleNames;
import systems.zlink.samples.bingo.server.configuration.SampleTimings;
import systems.zlink.samples.bingo.server.configuration.SampleTopology;
import systems.zlink.samples.bingo.shared.contracts.BingoMessages;
import systems.zlink.samples.bingo.shared.contracts.Messages;

public final class AuthenticateSessionHandler
    implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, Messages.AuthenticateReq> {
    private final ZLinkClient channels;
    private final ZLinkRouteClient routes;

    public AuthenticateSessionHandler(ZLinkClient channels, ZLinkRouteClient routes) {
        this.channels = channels;
        this.routes = routes;
    }

    @Override
    public String packetName() {
        return "AuthenticateReq";
    }

    @Override
    public Class<Messages.AuthenticateReq> messageType() {
        return Messages.AuthenticateReq.class;
    }

    @Override
    public void handle(
        ZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        Messages.AuthenticateReq request) {
        if (request.getAccessToken().isBlank()) {
            throw new IllegalArgumentException("access token is required");
        }
        var authenticated = channels
            .requestToChannel(SampleNames.ApiChannel, BingoMessages.authenticatePlayerReq(request.getAccessToken()))
            .timeout(SampleTimings.RequestTimeout)
            .await(Messages.AuthenticatePlayerRes.class);
        if (!authenticated.getAccepted()
            || authenticated.getActorId().isBlank()
            || authenticated.getDisplayName().isBlank()) {
            throw new IllegalStateException(
                authenticated.getReason().isBlank()
                    ? "Player authentication failed."
                    : authenticated.getReason());
        }
        RoutingId preferredPlayNode = RoutingId.from(SampleTopology.preferredPlayNodeRid());
        var ensured = routes
            .requestToSpot(
                SampleNames.RoomSpotDiscovery,
                new SpotRef(SampleNames.RoomSpotDiscovery, preferredPlayNode, preferredPlayNode),
                BingoMessages.ensurePlayerActorReq(
                    authenticated.getActorId(),
                    authenticated.getDisplayName(),
                    SampleTopology.preferredPlayNodeRid()))
            .timeout(SampleTimings.RequestTimeout)
            .await(Messages.EnsurePlayerActorRes.class);
        await(context.actors().bind(new ActorRef(
            RoutingId.from(ensured.getActor().getNodeRid()),
            ensured.getActor().getActorId(),
            ensured.getActor().getGeneration())));
        context.client()
            .reply(BingoMessages.authenticateRes(
                ensured.getActorId(),
                authenticated.getDisplayName(),
                ensured.getActor().getNodeRid()))
            .await();
    }
}
