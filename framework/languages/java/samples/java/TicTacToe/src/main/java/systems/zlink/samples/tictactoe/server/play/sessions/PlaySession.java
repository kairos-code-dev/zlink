package systems.zlink.samples.tictactoe.server.play.sessions;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkStreamError;
import systems.zlink.framework.streams.ZLinkStreamHeader;
import systems.zlink.samples.tictactoe.server.configuration.SampleNames;
import systems.zlink.samples.tictactoe.server.play.actors.PlayActor;
import systems.zlink.samples.tictactoe.server.play.gamespots.TicTacToeGameDirectory;
import systems.zlink.samples.tictactoe.shared.contracts.AuthenticateReq;
import systems.zlink.samples.tictactoe.shared.contracts.AuthenticateRes;
import systems.zlink.samples.tictactoe.shared.contracts.JoinGameReq;
import systems.zlink.samples.tictactoe.shared.contracts.PlaceMarkReq;
import systems.zlink.stream.connector.ZLinkStreamEncodedPayload;
import systems.zlink.stream.connector.json.ZLinkStreamJson;

public final class PlaySession implements ZLinkSession {
    private final ZLinkSessionContext context;
    private final ZLinkActorManager actors;
    private String actorId;
    private PlayActor playActor;
    private ZLinkSessionActor actor;

    public PlaySession(
        ZLinkSessionContext context,
        ZLinkActorManager actors) {
        this.context = context;
        this.actors = actors;
    }

    @Override
    public ZLinkSessionContext context() {
        return context;
    }

    @Override
    public CompletionStage<Void> onConnectedAsync() {
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public CompletionStage<Void> onDisconnectedAsync() {
        actorId = null;
        playActor = null;
        actor = null;
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public CompletionStage<Void> onErrorAsync(ZLinkStreamError error) {
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public CompletionStage<Void> onDispatchAsync(ZLinkStreamHeader header, Message payload) {
        try {
            if (header.packetName().equals("AuthenticateReq")) {
                AuthenticateReq request = decode(header, payload, AuthenticateReq.class);
                actorId = request.actorId();
                return actors.getOrCreateAsync(actorId, SampleNames.PlayActor)
                    .thenCompose(created -> {
                        playActor = (PlayActor) created;
                        return context.actors().bindAsync(created);
                    })
                    .thenCompose(bound -> {
                        actor = bound;
                        return context.client()
                            .reply(new AuthenticateRes(actorId))
                            .submitAsync();
                    });
            }
            if (header.packetName().equals("JoinGameReq")) {
                JoinGameReq request = decode(header, payload, JoinGameReq.class);
                PlayActor playActor = requirePlayActor();
                playActor.joinGame(request.gameId());
                return context.client()
                    .reply(TicTacToeGameDirectory.get(request.gameId())
                        .join(playActor.actorId()))
                    .submitAsync();
            }
            if (header.packetName().equals("PlaceMarkReq")) {
                PlaceMarkReq request = decode(header, payload, PlaceMarkReq.class);
                PlayActor playActor = requirePlayActor();
                return context.client()
                    .reply(TicTacToeGameDirectory.get(playActor.gameId())
                        .placeMark(playActor.actorId(), request.cell()))
                    .submitAsync();
            }
            return requireActor().relayAsync(header, payload);
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }

    private ZLinkSessionActor requireActor() {
        if (actorId == null || actorId.isBlank() || actor == null) {
            throw new IllegalStateException("AuthenticateReq is required before play packets.");
        }
        return actor;
    }

    private PlayActor requirePlayActor() {
        requireActor();
        if (playActor == null) {
            throw new IllegalStateException("bound PlayActor is missing.");
        }
        return playActor;
    }

    private static <T> T decode(ZLinkStreamHeader header, Message payload, Class<T> type) {
        return ZLinkStreamJson.decode(
            new ZLinkStreamEncodedPayload(header.packetName(), payload, header.metadata()),
            type);
    }
}
