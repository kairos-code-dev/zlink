package systems.zlink.samples.tictactoe.sessiongateway.client;

import java.net.URI;
import java.time.Duration;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory;
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions;
import systems.zlink.stream.connector.ZLinkStreamDispatchMode;
import systems.zlink.stream.connector.ZLinkStreamEncodedPayload;

public final class SessionActorDispatchPlayerClient implements AutoCloseable {
    private final String actorId;
    private final SessionActorNotificationInbox notifications;
    private final ZLinkStreamConnector connector;

    private SessionActorDispatchPlayerClient(
        String actorId,
        SessionActorNotificationInbox notifications,
        ZLinkStreamConnector connector) {
        this.actorId = actorId;
        this.notifications = notifications;
        this.connector = connector;
    }

    public static SessionActorDispatchPlayerClient connect(
        String actorId,
        String streamEndpoint) throws Exception {
        ZLinkStreamConnector connector = ZLinkStreamConnectorFactory.create(new ZLinkStreamConnectorOptions(
            URI.create(streamEndpoint + "/" + actorId),
            ZLinkStreamDispatchMode.MANUAL,
            Duration.ofSeconds(10),
            2));
        await(connector.connectAsync());
        SessionActorNotificationInbox inbox = new SessionActorNotificationInbox();
        inbox.register(connector);
        return new SessionActorDispatchPlayerClient(actorId, inbox, connector);
    }

    public AuthenticateRes authenticate() throws Exception {
        return new AuthenticateRes(request("AuthenticateReq", actorId));
    }

    public CreateMatchRes createMatch() throws Exception {
        String[] parts = request("CreateMatchReq", "").split("\\|", -1);
        return new CreateMatchRes(parts[0], parts[1]);
    }

    public JoinMatchRes join(String matchId) throws Exception {
        String[] parts = request("JoinMatchReq", matchId).split("\\|", -1);
        return new JoinMatchRes(parts[0], parts[1], parts[2], TicTacToeState.decode(parts[3]));
    }

    public PlaceMarkRes placeMark(String matchId, int cell) throws Exception {
        return new PlaceMarkRes(TicTacToeState.decode(request("PlaceMarkReq", matchId + "|" + cell)));
    }

    public void dispatch() throws Exception {
        await(connector.dispatchAsync());
    }

    public int notificationCount() {
        return turnChangedNotifications().size()
            + opponentJoinedNotifications().size()
            + gameEndedNotifications().size();
    }

    public java.util.List<String> turnChangedNotifications() {
        return notifications.turnChanged();
    }

    public java.util.List<String> opponentJoinedNotifications() {
        return notifications.opponentJoined();
    }

    public java.util.List<String> gameEndedNotifications() {
        return notifications.gameEnded();
    }

    @Override
    public void close() {
        connector.close();
    }

    private String request(String packetName, String body) throws Exception {
        return await(connector.request(new ZLinkStreamEncodedPayload(
                packetName,
                Message.from(body),
                Map.of()))
            .packetName(packetName)
            .submitAsync())
            .payload()
            .toUtf8String();
    }

    private static <T> T await(CompletionStage<T> stage) throws Exception {
        CompletableFuture<T> done = new CompletableFuture<>();
        stage.whenComplete((value, error) -> {
            if (error != null) {
                done.completeExceptionally(error);
            } else {
                done.complete(value);
            }
        });
        return done.get();
    }

    public record AuthenticateRes(String actorId) {
    }

    public record CreateMatchRes(String matchId, String ownerActorId) {
    }

    public record JoinMatchRes(
        String matchId,
        String actorId,
        String mark,
        TicTacToeState state) {
    }

    public record PlaceMarkRes(TicTacToeState state) {
    }

    public record TicTacToeState(
        String matchId,
        String board,
        String status,
        String turnActorId,
        String winnerActorId,
        boolean draw,
        String xActorId,
        String oActorId,
        String lastMoveActorId,
        Integer lastMoveCell) {
        static TicTacToeState decode(String value) {
            String[] parts = value.split(",", -1);
            return new TicTacToeState(
                parts[0],
                parts[1],
                parts[2],
                emptyToNull(parts[3]),
                emptyToNull(parts[4]),
                Boolean.parseBoolean(parts[5]),
                emptyToNull(parts[6]),
                emptyToNull(parts[7]),
                emptyToNull(parts[8]),
                parts[9].isBlank() ? null : Integer.parseInt(parts[9]));
        }
    }

    private static String emptyToNull(String value) {
        return value.isEmpty() ? null : value;
    }
}
