package systems.zlink.samples.bingo.server.session.sessions.handlers;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkStreamHeader;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;
import systems.zlink.samples.bingo.server.configuration.SampleNames;
import systems.zlink.samples.bingo.server.configuration.SampleTimings;
import systems.zlink.samples.bingo.server.configuration.SampleTopology;
import systems.zlink.samples.bingo.shared.contracts.Messages;

public final class AuthenticateSessionHandler
    implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, Messages.AuthenticateReq> {
    private final ZLinkClient channels;
    private final ZLinkRouteClient routes;

    public AuthenticateSessionHandler(
        ZLinkClient channels,
        ZLinkRouteClient routes) {
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
	        ZLinkStreamHeader header,
	        Messages.AuthenticateReq request) {
	        trace("authenticate request received token=" + request.accessToken());
	        if (request.accessToken() == null || request.accessToken().isBlank()) {
	            throw new IllegalArgumentException("access token is required");
	        }
	        trace("request api authentication");
	        var authenticated = channels
	            .requestToChannel(SampleNames.ApiChannel, new Messages.AuthenticatePlayerReq(request.accessToken()))
	            .timeout(SampleTimings.RequestTimeout)
	            .await(Messages.AuthenticatePlayerRes.class);
	        trace("api authentication accepted=" + authenticated.accepted());
	        if (!authenticated.accepted()
            || authenticated.actorId() == null
            || authenticated.actorId().isBlank()
            || authenticated.displayName() == null
            || authenticated.displayName().isBlank()) {
            throw new IllegalStateException(
                authenticated.reason() == null
                    ? "Player authentication failed."
                    : authenticated.reason());
	        }
	        trace("request play actor ensure preferred=" + SampleTopology.preferredPlayNodeRid());
	        var ensured = routes
            .requestTo(
                SampleNames.PlayChannel,
                RoutingId.from(SampleTopology.preferredPlayNodeRid()),
                new Messages.EnsurePlayerActorReq(
                    authenticated.actorId(),
                    authenticated.displayName(),
                    SampleTopology.preferredPlayNodeRid()))
	            .timeout(SampleTimings.RequestTimeout)
	            .await(Messages.EnsurePlayerActorRes.class);
	        trace("play actor ensured actor=" + ensured.actorId());
	        await(context.actors()
            .bind(new ZLinkActorRef(
                RoutingId.from(ensured.actor().nodeRid()),
                ensured.actor().actorId(),
                ensured.actor().generation())));
        context.client()
            .reply(new Messages.AuthenticateRes(
                ensured.actorId(),
                authenticated.displayName(),
                SampleTopology.preferredPlayNodeRid()))
	            .await();
	        trace("authenticate reply sent actor=" + ensured.actorId());
	    }

	    private static void trace(String message) {
	        System.out.println("bingo session: " + message);
	    }
	}
