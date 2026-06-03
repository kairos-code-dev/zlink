package systems.zlink.samples.tictactoe.sessiongateway.client;

import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.locks.LockSupport;
import systems.zlink.samples.tictactoe.sessiongateway.client.SessionActorDispatchPlayerClient.AuthenticateRes;
import systems.zlink.samples.tictactoe.sessiongateway.client.SessionActorDispatchPlayerClient.CreateMatchRes;
import systems.zlink.samples.tictactoe.sessiongateway.client.SessionActorDispatchPlayerClient.JoinMatchRes;
import systems.zlink.samples.tictactoe.sessiongateway.client.SessionActorDispatchPlayerClient.PlaceMarkRes;
import systems.zlink.samples.tictactoe.sessiongateway.client.SessionActorDispatchPlayerClient.TicTacToeState;

public final class SessionActorDispatchClient {
    public SessionActorDispatchClientResult run(SessionActorDispatchClientOptions options) throws Exception {
        try (SessionActorDispatchPlayerClient xClient = SessionActorDispatchPlayerClient.connect(
                options.xActorId(),
                options.primaryStreamEndpoint());
             SessionActorDispatchPlayerClient oClient = SessionActorDispatchPlayerClient.connect(
                options.oActorId(),
                options.reconnectStreamEndpoint())) {
            AuthenticateRes xAuth = xClient.authenticate();
            AuthenticateRes oAuth = oClient.authenticate();
            CreateMatchRes created = xClient.createMatch();

            xClient.close();
            try (SessionActorDispatchPlayerClient reconnectedXClient = SessionActorDispatchPlayerClient.connect(
                    options.xActorId(),
                    options.reconnectStreamEndpoint())) {
                AuthenticateRes xReconnectAuth = reconnectedXClient.authenticate();
                JoinMatchRes xJoin = reconnectedXClient.join(created.matchId());
                JoinMatchRes oJoin = oClient.join(created.matchId());
                List<PlaceMarkRes> moves = List.of(
                    reconnectedXClient.placeMark(created.matchId(), 0),
                    oClient.placeMark(created.matchId(), 3),
                    reconnectedXClient.placeMark(created.matchId(), 1),
                    oClient.placeMark(created.matchId(), 4),
                    reconnectedXClient.placeMark(created.matchId(), 2));

                validateReconnect(options, xAuth, xReconnectAuth, xJoin.state());
                validateFinalState(options, moves.get(moves.size() - 1).state());
                waitForNotifications(reconnectedXClient, oClient);
                validateNotifications(reconnectedXClient, oClient);

                List<String> notifications = new ArrayList<>();
                notifications.addAll(reconnectedXClient.turnChangedNotifications());
                notifications.addAll(oClient.turnChangedNotifications());
                notifications.addAll(reconnectedXClient.opponentJoinedNotifications());
                notifications.addAll(oClient.opponentJoinedNotifications());
                notifications.addAll(reconnectedXClient.gameEndedNotifications());
                notifications.addAll(oClient.gameEndedNotifications());
                return new SessionActorDispatchClientResult(
                    xAuth,
                    oAuth,
                    xReconnectAuth,
                    created,
                    xJoin,
                    oJoin,
                    moves,
                    notifications,
                    reconnectedXClient.notificationCount() + oClient.notificationCount());
            }
        }
    }

    public void runReconnectScenario(SessionActorDispatchClientOptions options) throws Exception {
        run(options).writeTo(new PrintWriter(System.out, true));
    }

    private static void validateFinalState(
        SessionActorDispatchClientOptions options,
        TicTacToeState state) {
        if (!"Won".equals(state.status())
            || !options.xActorId().equals(state.winnerActorId())
            || !"XXXOO....".equals(state.board())) {
            throw new IllegalStateException(
                "Unexpected final state: board=" + state.board()
                    + ", status=" + state.status()
                    + ", winner=" + state.winnerActorId());
        }
    }

    private static void validateReconnect(
        SessionActorDispatchClientOptions options,
        AuthenticateRes firstAuth,
        AuthenticateRes reconnectAuth,
        TicTacToeState reconnectedJoinState) {
        if (!options.xActorId().equals(firstAuth.actorId())
            || !options.xActorId().equals(reconnectAuth.actorId())
            || !options.xActorId().equals(reconnectedJoinState.xActorId())) {
            throw new IllegalStateException("Reconnect did not recover the same actor binding.");
        }
    }

    private static void waitForNotifications(
        SessionActorDispatchPlayerClient xClient,
        SessionActorDispatchPlayerClient oClient) throws Exception {
        long deadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(3);
        while (System.nanoTime() < deadline) {
            xClient.dispatch();
            oClient.dispatch();
            if (hasRequiredNotificationSmoke(xClient, oClient)) {
                return;
            }
            LockSupport.parkNanos(java.util.concurrent.TimeUnit.MILLISECONDS.toNanos(10));
        }
        throw new IllegalStateException("Timed out waiting for push notifications. " + notificationCounts(xClient, oClient));
    }

    private static void validateNotifications(
        SessionActorDispatchPlayerClient xClient,
        SessionActorDispatchPlayerClient oClient) {
        if (!hasRequiredNotificationSmoke(xClient, oClient)) {
            throw new IllegalStateException(
                "Session actor dispatch push notifications did not reach both actors. "
                    + notificationCounts(xClient, oClient));
        }
    }

    private static boolean hasRequiredNotificationSmoke(
        SessionActorDispatchPlayerClient xClient,
        SessionActorDispatchPlayerClient oClient) {
        return xClient.notificationCount() > 0 && oClient.notificationCount() > 0;
    }

    private static String notificationCounts(
        SessionActorDispatchPlayerClient xClient,
        SessionActorDispatchPlayerClient oClient) {
        return "x(turn=" + xClient.turnChangedNotifications().size()
            + ", joined=" + xClient.opponentJoinedNotifications().size()
            + ", ended=" + xClient.gameEndedNotifications().size()
            + "), o(turn=" + oClient.turnChangedNotifications().size()
            + ", joined=" + oClient.opponentJoinedNotifications().size()
            + ", ended=" + oClient.gameEndedNotifications().size()
            + ")";
    }

    public record SessionActorDispatchClientResult(
        AuthenticateRes xAuthentication,
        AuthenticateRes oAuthentication,
        AuthenticateRes xReconnectAuthentication,
        CreateMatchRes createdMatch,
        JoinMatchRes xJoin,
        JoinMatchRes oJoin,
        List<PlaceMarkRes> moves,
        List<String> notifications,
        int notificationCount) {
        public void writeTo(PrintWriter writer) {
            TicTacToeState finalState = moves.get(moves.size() - 1).state();
            writer.println("auth=" + xAuthentication.actorId() + "," + oAuthentication.actorId());
            writer.println("created=" + createdMatch.matchId() + ", reconnect=" + xReconnectAuthentication.actorId());
            writer.println("players=X:" + xJoin.state().xActorId() + ", O:" + oJoin.state().oActorId());
            writer.println("final=" + finalState.board()
                + ", status=" + finalState.status()
                + ", winner=" + finalState.winnerActorId());
            writer.println("notifications=" + notificationCount);
        }
    }
}
