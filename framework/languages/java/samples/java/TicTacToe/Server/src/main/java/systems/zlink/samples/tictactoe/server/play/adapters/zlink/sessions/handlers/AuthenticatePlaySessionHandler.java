package systems.zlink.samples.tictactoe.server.play.adapters.zlink.sessions.handlers;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkStreamHeader;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;
import systems.zlink.samples.tictactoe.server.configuration.SampleNames;
import systems.zlink.samples.tictactoe.shared.contracts.AuthenticatePlayerReq;
import systems.zlink.samples.tictactoe.shared.contracts.AuthenticatePlayerRes;
import systems.zlink.samples.tictactoe.shared.contracts.AuthenticateReq;
import systems.zlink.samples.tictactoe.shared.contracts.AuthenticateRes;

public final class AuthenticatePlaySessionHandler
    implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, AuthenticateReq> {
    private final ZLinkActorManager actors;
    private final ZLinkClient channels;

    public AuthenticatePlaySessionHandler(
        ZLinkActorManager actors,
        ZLinkClient channels) {
        this.actors = actors;
        this.channels = channels;
    }

    @Override
    public String packetName() {
        return "AuthenticateReq";
    }

    @Override
    public Class<AuthenticateReq> messageType() {
        return AuthenticateReq.class;
    }

    @Override
    public void handle(
        ZLinkSessionContext context,
        ZLinkStreamHeader header,
        AuthenticateReq request) {
        if (request.accessToken() == null || request.accessToken().isBlank()) {
            throw new IllegalArgumentException("access token is required");
        }
        AuthenticatePlayerRes authenticated = channels
            .requestToChannel(
                SampleNames.ApiChannel,
                new AuthenticatePlayerReq(request.accessToken()))
            .timeout(SampleNames.RequestTimeout)
            .await(AuthenticatePlayerRes.class);
        var playActor = await(actors.getOrCreate(authenticated.player().actorId(), SampleNames.PlayActor));
        ((systems.zlink.samples.tictactoe.server.play.adapters.zlink.actors.PlayActor) playActor)
            .applyPlayer(authenticated.player());
        var bound = await(context.actors().bind(playActor));
        context.client()
            .reply(new AuthenticateRes(authenticated.player()))
            .await();
    }
}
