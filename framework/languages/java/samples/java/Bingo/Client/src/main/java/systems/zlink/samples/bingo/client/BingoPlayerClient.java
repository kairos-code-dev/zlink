package systems.zlink.samples.bingo.client;

import java.net.URI;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.TimeoutException;
import java.util.function.Function;
import systems.zlink.samples.bingo.shared.configuration.SampleNames;
import systems.zlink.samples.bingo.shared.configuration.SampleTimings;
import systems.zlink.samples.bingo.shared.configuration.SampleTopology;
import systems.zlink.samples.bingo.shared.contracts.Messages;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory;
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions;
import systems.zlink.stream.connector.ZLinkStreamDispatchMode;
import systems.zlink.stream.connector.json.ZLinkStreamJson;

public final class BingoPlayerClient implements AutoCloseable {
    private final String playerId;
    private final ZLinkStreamConnector connector;
    private final BingoNotificationInbox inbox = new BingoNotificationInbox();

    public BingoPlayerClient(String playerId) {
        this.playerId = playerId;
        this.connector = ZLinkStreamConnectorFactory.create(new ZLinkStreamConnectorOptions(
            URI.create(SampleTopology.StreamEndpoint),
            ZLinkStreamDispatchMode.MANUAL,
            SampleTimings.RequestTimeout,
            2,
            SampleTimings.ConnectTimeout,
            64 * 1024,
            false,
            java.time.Duration.ofSeconds(1),
            SampleTimings.RequestTimeout.plusSeconds(5),
            true,
            java.time.Duration.ofMillis(250),
            java.time.Duration.ofSeconds(5),
            2.0));
    }

    public CompletionStage<Void> connectAsync() {
        connector.on(SampleNames.PlayerJoinedPacket, message -> {
            inbox.playerJoined(
                ZLinkStreamJson.decode(message.payload(), Messages.PlayerJoinedNotify.class));
            return CompletableFuture.completedFuture(null);
        });
        connector.on(SampleNames.GameStartedPacket, message -> {
            inbox.started(
                ZLinkStreamJson.decode(message.payload(), Messages.BingoGameStartedNotify.class));
            return CompletableFuture.completedFuture(null);
        });
        connector.on(SampleNames.NumberDrawnPacket, message -> {
            inbox.drawn(
                ZLinkStreamJson.decode(message.payload(), Messages.BingoNumberDrawnNotify.class));
            return CompletableFuture.completedFuture(null);
        });
        connector.on(SampleNames.GameEndedPacket, message -> {
            inbox.ended(
                ZLinkStreamJson.decode(message.payload(), Messages.BingoGameEndedNotify.class));
            return CompletableFuture.completedFuture(null);
        });
        return connector.connectAsync();
    }

    public CompletionStage<Messages.AuthenticateRes> authenticateAsync() {
        return submitWithDispatch(ZLinkStreamJson.request(connector, new Messages.AuthenticateReq(playerId))
            .timeout(SampleTimings.RequestTimeout)
            .submitAsync())
            .thenApply(reply -> ZLinkStreamJson.decode(
                reply,
                Messages.AuthenticateRes.class));
    }

    public CompletionStage<Messages.MatchBingoRes> matchAsync() {
        return matchAsync(0);
    }

    private CompletionStage<Messages.MatchBingoRes> matchAsync(int attempt) {
        return submitWithDispatch(ZLinkStreamJson.request(connector, new Messages.MatchBingoReq("four-player"))
            .timeout(SampleTimings.RequestTimeout)
            .submitAsync())
            .thenApply(reply -> ZLinkStreamJson.decode(
                reply,
                Messages.MatchBingoRes.class))
            .handle((reply, error) -> {
                if (error == null) {
                    return CompletableFuture.completedFuture(reply);
                }
                Throwable cause = unwrap(error);
                if (attempt < 2 && cause instanceof TimeoutException) {
                    return matchAsync(attempt + 1);
                }
                return CompletableFuture.<Messages.MatchBingoRes>failedFuture(cause);
            })
            .thenCompose(Function.identity());
    }

    public CompletionStage<Messages.StartBingoGameRes> startAsync(String roomId) {
        return submitWithDispatch(ZLinkStreamJson.request(connector, new Messages.StartBingoGameReq(roomId))
            .timeout(SampleTimings.RequestTimeout)
            .submitAsync())
            .thenApply(reply -> ZLinkStreamJson.decode(
                reply,
                Messages.StartBingoGameRes.class));
    }

    public CompletionStage<Void> dispatchAsync() {
        return connector.dispatchAsync();
    }

    public String playerId() {
        return playerId;
    }

    public BingoNotificationInbox inbox() {
        return inbox;
    }

    public List<Messages.BingoWinner> winners() {
        return inbox.winners();
    }

    private <T> CompletionStage<T> submitWithDispatch(CompletionStage<T> stage) {
        CompletableFuture<T> result = new CompletableFuture<>();
        stage.whenComplete((value, error) -> {
            if (error != null) {
                result.completeExceptionally(error);
            } else {
                result.complete(value);
            }
        });
        return CompletableFuture.supplyAsync(() -> {
            while (!result.isDone()) {
                awaitUnchecked(connector.dispatchAsync());
                Thread.onSpinWait();
            }
            return result.join();
        });
    }

    private static void awaitUnchecked(CompletionStage<Void> stage) {
        CompletableFuture<Void> done = new CompletableFuture<>();
        stage.whenComplete((value, error) -> {
            if (error != null) {
                done.completeExceptionally(error);
            } else {
                done.complete(null);
            }
        });
        done.join();
    }

    private static Throwable unwrap(Throwable error) {
        Throwable current = error;
        while (current instanceof CompletionException && current.getCause() != null) {
            current = current.getCause();
        }
        return current;
    }

    @Override
    public void close() {
        connector.close();
    }
}
