package systems.zlink.samples.tictactoe.client;

import java.net.URI;
import java.time.Duration;
import systems.zlink.httpclient.ZLinkHttpClient;
import systems.zlink.samples.tictactoe.shared.contracts.AuthenticateReq;
import systems.zlink.samples.tictactoe.shared.contracts.AuthenticateRes;
import systems.zlink.samples.tictactoe.shared.contracts.CreateGameHttpReq;
import systems.zlink.samples.tictactoe.shared.contracts.CreateGameHttpRes;
import systems.zlink.samples.tictactoe.shared.contracts.GameStateNotify;
import systems.zlink.samples.tictactoe.shared.contracts.JoinGameReq;
import systems.zlink.samples.tictactoe.shared.contracts.JoinGameRes;
import systems.zlink.samples.tictactoe.shared.contracts.LeaveGameReq;
import systems.zlink.samples.tictactoe.shared.contracts.ObserveMilestoneReq;
import systems.zlink.samples.tictactoe.shared.contracts.ObserveMilestoneRes;
import systems.zlink.samples.tictactoe.shared.contracts.PlaceMarkReq;
import systems.zlink.samples.tictactoe.shared.contracts.PlaceMarkRes;
import systems.zlink.samples.tictactoe.shared.contracts.PlayerJoinedNotify;
import systems.zlink.samples.tictactoe.shared.contracts.WinMilestoneNotify;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory;
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions;
import systems.zlink.stream.connector.ZLinkStreamDispatchMode;
import systems.zlink.framework.codecs.msgpack.ZLinkMessagePackCodec;

public final class TicTacToeClientScenario {
    public void run(TicTacToeClientOptions options) throws Exception {
        CreateGameHttpRes game;
        try (ZLinkHttpClient api = ZLinkHttpClient.create(options.apiUrl()).build()) {
            game = api.post("/games")
                .body(new CreateGameHttpReq(options.gameName()))
                .fetch(CreateGameHttpRes.class);
        }
        String observerEndpoint = nonOwnerEndpoint(game);
        ZLinkStreamConnector host = playerConnector(game.ownerPlayEndpoint());
        ZLinkStreamConnector guest = playerConnector(observerEndpoint);
        ZLinkStreamConnector observer = playerConnector(observerEndpoint);

        try {
            host.connect().await();
            guest.connect().await();
            observer.connect().await();

            ensure(game.roomId() != null && !game.roomId().isBlank());
            ensure(game.ownerPlayEndpoint() != null && !game.ownerPlayEndpoint().isBlank());
            ensure(options.gameName().equals(game.gameName()));
            ensure(game.requiredLevel() == 3);

            AuthenticateRes hostAuth = host
                .request(new AuthenticateReq(options.xActorId()))
                .await(AuthenticateRes.class);
            ensure(options.xActorId().equals(hostAuth.player().actorId()));
            ensure(hostAuth.player().wins() == 99);

            AuthenticateRes guestAuth = guest
                .request(new AuthenticateReq(options.oActorId()))
                .await(AuthenticateRes.class);
            ensure(options.oActorId().equals(guestAuth.player().actorId()));
            ensure(!guestAuth.player().actorId().equals(hostAuth.player().actorId()));

            AuthenticateRes observerAuth = observer
                .request(new AuthenticateReq(options.observerActorId()))
                .await(AuthenticateRes.class);
            ensure(options.observerActorId().equals(observerAuth.player().actorId()));
            ObserveMilestoneRes subscribed = observer
                .request(new ObserveMilestoneReq())
                .await(ObserveMilestoneRes.class);
            ensure(subscribed.subscribed());
            System.out.println("observer-connected endpoint=" + observerEndpoint);
            System.out.println("observer-subscription=verified subscribed=" + subscribed.subscribed());

            JoinGameRes hostJoin = host
                .request(new JoinGameReq(game.roomId()))
                .await(JoinGameRes.class);
            ensure(hostJoin.state().roomId().equals(game.roomId()));
            ensure("WaitingForPlayers".equals(hostJoin.state().status()));
            ensure(options.xActorId().equals(hostJoin.state().xActorId()));
            ensure(host.receivedCount("PlayerJoinedNotify") == 0);

            var hostSawGuestJoin = host
                .waitFor(PlayerJoinedNotify.class)
                .where(PlayerJoinedNotify.class,
                    message -> options.oActorId().equals(message.payload().actorId()))
                .submit(PlayerJoinedNotify.class);
            var hostSawGameStart = host
                .waitFor(GameStateNotify.class)
                .where(GameStateNotify.class,
                    message -> "InProgress".equals(message.payload().state().status())
                        && options.oActorId().equals(message.payload().state().oActorId()))
                .submit(GameStateNotify.class);

            JoinGameRes guestJoin = guest
                .request(new JoinGameReq(game.roomId()))
                .await(JoinGameRes.class);
            ensure(guestJoin.state().roomId().equals(game.roomId()));
            ensure("InProgress".equals(guestJoin.state().status()));
            ensure(options.oActorId().equals(guestJoin.state().oActorId()));

            PlayerJoinedNotify guestJoinNotify = host.await(hostSawGuestJoin).payload();
            ensure("O".equals(guestJoinNotify.mark()));
            ensure("InProgress".equals(guestJoinNotify.state().status()));
            ensure(guest.receivedCount("PlayerJoinedNotify") == 0);

            GameStateNotify gameStart = host.await(hostSawGameStart).payload();
            ensure("X".equals(gameStart.state().nextTurn()));

            var guestSawHostMove1 = guest
                .waitFor(GameStateNotify.class)
                .where(GameStateNotify.class,
                    message -> options.xActorId().equals(message.payload().state().lastMoveActorId())
                        && Integer.valueOf(0).equals(message.payload().state().lastMoveCell()))
                .submit(GameStateNotify.class);
            PlaceMarkRes hostMove1 = host
                .request(new PlaceMarkReq(0))
                .await(PlaceMarkRes.class);
            ensure("X........".equals(hostMove1.state().board()));
            ensure("O".equals(hostMove1.state().nextTurn()));
            ensure(options.xActorId().equals(hostMove1.state().lastMoveActorId()));
            ensure(Integer.valueOf(0).equals(hostMove1.state().lastMoveCell()));

            GameStateNotify hostMove1Notify = guest.await(guestSawHostMove1).payload();
            ensure(hostMove1Notify.state().board().equals(hostMove1.state().board()));
            ensure("O".equals(hostMove1Notify.state().nextTurn()));
            ensure(options.xActorId().equals(hostMove1Notify.state().lastMoveActorId()));
            ensure(Integer.valueOf(0).equals(hostMove1Notify.state().lastMoveCell()));

            var hostSawGuestMove1 = host
                .waitFor(GameStateNotify.class)
                .where(GameStateNotify.class,
                    message -> options.oActorId().equals(message.payload().state().lastMoveActorId())
                        && Integer.valueOf(3).equals(message.payload().state().lastMoveCell()))
                .submit(GameStateNotify.class);
            PlaceMarkRes guestMove1 = guest
                .request(new PlaceMarkReq(3))
                .await(PlaceMarkRes.class);
            ensure("X..O.....".equals(guestMove1.state().board()));
            ensure("X".equals(guestMove1.state().nextTurn()));
            ensure(options.oActorId().equals(guestMove1.state().lastMoveActorId()));
            ensure(Integer.valueOf(3).equals(guestMove1.state().lastMoveCell()));

            GameStateNotify guestMove1Notify = host.await(hostSawGuestMove1).payload();
            ensure(guestMove1Notify.state().board().equals(guestMove1.state().board()));
            ensure("X".equals(guestMove1Notify.state().nextTurn()));
            ensure(options.oActorId().equals(guestMove1Notify.state().lastMoveActorId()));
            ensure(Integer.valueOf(3).equals(guestMove1Notify.state().lastMoveCell()));

            var guestSawHostMove2 = guest
                .waitFor(GameStateNotify.class)
                .where(GameStateNotify.class,
                    message -> options.xActorId().equals(message.payload().state().lastMoveActorId())
                        && Integer.valueOf(1).equals(message.payload().state().lastMoveCell()))
                .submit(GameStateNotify.class);
            PlaceMarkRes hostMove2 = host
                .request(new PlaceMarkReq(1))
                .await(PlaceMarkRes.class);
            ensure("XX.O.....".equals(hostMove2.state().board()));
            ensure("O".equals(hostMove2.state().nextTurn()));
            ensure(options.xActorId().equals(hostMove2.state().lastMoveActorId()));
            ensure(Integer.valueOf(1).equals(hostMove2.state().lastMoveCell()));

            GameStateNotify hostMove2Notify = guest.await(guestSawHostMove2).payload();
            ensure(hostMove2Notify.state().board().equals(hostMove2.state().board()));
            ensure("O".equals(hostMove2Notify.state().nextTurn()));
            ensure(options.xActorId().equals(hostMove2Notify.state().lastMoveActorId()));
            ensure(Integer.valueOf(1).equals(hostMove2Notify.state().lastMoveCell()));

            var hostSawGuestMove2 = host
                .waitFor(GameStateNotify.class)
                .where(GameStateNotify.class,
                    message -> options.oActorId().equals(message.payload().state().lastMoveActorId())
                        && Integer.valueOf(4).equals(message.payload().state().lastMoveCell()))
                .submit(GameStateNotify.class);
            PlaceMarkRes guestMove2 = guest
                .request(new PlaceMarkReq(4))
                .await(PlaceMarkRes.class);
            ensure("XX.OO....".equals(guestMove2.state().board()));
            ensure("X".equals(guestMove2.state().nextTurn()));
            ensure(options.oActorId().equals(guestMove2.state().lastMoveActorId()));
            ensure(Integer.valueOf(4).equals(guestMove2.state().lastMoveCell()));

            GameStateNotify guestMove2Notify = host.await(hostSawGuestMove2).payload();
            ensure(guestMove2Notify.state().board().equals(guestMove2.state().board()));
            ensure("X".equals(guestMove2Notify.state().nextTurn()));
            ensure(options.oActorId().equals(guestMove2Notify.state().lastMoveActorId()));
            ensure(Integer.valueOf(4).equals(guestMove2Notify.state().lastMoveCell()));

            var guestSawHostWin = guest
                .waitFor(GameStateNotify.class)
                .where(GameStateNotify.class,
                    message -> "Won".equals(message.payload().state().status())
                        && options.xActorId().equals(message.payload().state().winner()))
                .submit(GameStateNotify.class);
            var observerSawMilestone = observer
                .waitFor(WinMilestoneNotify.class)
                .where(WinMilestoneNotify.class,
                    message -> options.xActorId().equals(message.payload().actorId())
                        && message.payload().wins() == 100)
                .submit(WinMilestoneNotify.class);
            PlaceMarkRes hostWin = host
                .request(new PlaceMarkReq(2))
                .await(PlaceMarkRes.class);
            ensure("XXXOO....".equals(hostWin.state().board()));
            ensure("Won".equals(hostWin.state().status()));
            ensure(options.xActorId().equals(hostWin.state().winner()));

            GameStateNotify hostWinNotify = guest.await(guestSawHostWin).payload();
            ensure(hostWinNotify.state().board().equals(hostWin.state().board()));
            ensure("Won".equals(hostWinNotify.state().status()));
            ensure(options.xActorId().equals(hostWinNotify.state().winner()));

            WinMilestoneNotify milestone = observer.await(observerSawMilestone).payload();
            String expectedRid = game.playNodes().stream()
                .filter(node -> observerEndpoint.equals(node.streamEndpoint()))
                .findFirst()
                .orElseThrow()
                .spotNodeRid();
            ensure(milestone.wins() == 100);
            ensure(expectedRid.equals(milestone.receivingSpotNodeRid()));
            System.out.println("observer-win-milestone=verified actor="
                + milestone.actorId()
                + " wins=" + milestone.wins()
                + " receivingSpotNodeRid=" + milestone.receivingSpotNodeRid());

            host.send(new LeaveGameReq(game.roomId())).await();
            guest.send(new LeaveGameReq(game.roomId())).await();
            System.out.println("tictactoe completed");
        } finally {
            host.close().await();
            guest.close().await();
            observer.close().await();
        }
    }

    private static String nonOwnerEndpoint(CreateGameHttpRes game) {
        return game.playEndpoints().stream()
            .filter(endpoint -> !endpoint.equals(game.ownerPlayEndpoint()))
            .findFirst()
            .orElseThrow(() -> new IllegalStateException("non-owner Play endpoint is required"));
    }

    private static ZLinkStreamConnector playerConnector(String endpoint) {
        return ZLinkStreamConnectorFactory.create(new ZLinkStreamConnectorOptions(
            URI.create(endpoint),
            ZLinkStreamDispatchMode.AUTO,
            TicTacToeSampleDefaults.RequestTimeout,
            2,
            Duration.ofSeconds(5),
            64 * 1024,
            false,
            Duration.ofSeconds(1),
            TicTacToeSampleDefaults.RequestTimeout.plusSeconds(5),
            true,
            Duration.ofMillis(250),
            Duration.ofSeconds(5),
            2.0,
            ZLinkMessagePackCodec.defaultCodec()));
    }

    private static void ensure(boolean condition) {
        if (!condition) {
            throw new IllegalStateException("Ensure failed");
        }
    }
}
