package systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.sessions.handlers;

import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
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
    public Class<AuthenticateReq> messageType() {
        return AuthenticateReq.class;
    }

    @Override
    public java.util.concurrent.CompletionStage<Void> handle(
        ZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        AuthenticateReq request) {
        if (request.accessToken() == null || request.accessToken().isBlank()) {
            throw new IllegalArgumentException("access token is required");
        }
        return channels
            .requestToChannel(
                SampleNames.ApiChannel,
                new AuthenticatePlayerReq(request.accessToken()))
            .timeout(SampleNames.RequestTimeout)
            .submit(AuthenticatePlayerRes.class)
            .thenCompose(authenticated -> actors.getOrCreate(
                    authenticated.player().actorId(), SampleNames.PlayActor, authenticated.player())
                .thenCompose(playActor -> context.actors().bind(playActor))
                .thenRun(() -> context.client()
                    .reply(new AuthenticateRes(authenticated.player()))
                    .submit()));
    }
}
