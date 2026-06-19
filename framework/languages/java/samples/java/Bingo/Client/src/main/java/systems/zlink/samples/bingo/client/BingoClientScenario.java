package systems.zlink.samples.bingo.client;

import java.util.ArrayList;
import java.util.List;
import systems.zlink.samples.bingo.client.configuration.SampleNames;
import systems.zlink.samples.bingo.shared.contracts.Messages;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class BingoClientScenario {
	    public void run(
	        ZLinkStreamConnector client1,
	        ZLinkStreamConnector client2) throws Exception {
	        trace("connect client1");
	        client1.connect().await();
	        trace("authenticate client1");
	        Messages.AuthenticateRes client1Auth =
	            client1.request(new Messages.AuthenticateReq("player-1")).await(Messages.AuthenticateRes.class);
	        ensure(client1Auth.actorId().equals("player-1"));

	        trace("match client1");
	        Messages.MatchBingoRes client1Match =
	            client1.request(new Messages.MatchBingoReq("two-player")).await(Messages.MatchBingoRes.class);
        ensure(client1Match.state().status().equals("WaitingForPlayers"));
        ensure(client1Match.state().hostActorId().equals(client1Auth.actorId()));
        ensure(client1.receivedCount(SampleNames.PlayerJoinedPacket) == 0);

        var client1SawClient2Join =
            client1.waitFor(SampleNames.PlayerJoinedPacket).submit(Messages.PlayerJoinedNotify.class);
        var client1Started =
            client1.waitFor(SampleNames.GameStartedPacket).submit(Messages.BingoGameStartedNotify.class);

	        trace("connect client2");
	        client2.connect().await();
	        trace("authenticate client2");
        Messages.AuthenticateRes client2Auth =
            client2.request(new Messages.AuthenticateReq("player-2")).await(Messages.AuthenticateRes.class);
        ensure(client2Auth.actorId().equals("player-2"));
        ensure(!client2Auth.actorId().equals(client1Auth.actorId()));
        var client2Started =
            client2.waitFor(SampleNames.GameStartedPacket).submit(Messages.BingoGameStartedNotify.class);

	        trace("match client2");
	        Messages.MatchBingoRes client2Match =
	            client2.request(new Messages.MatchBingoReq("two-player")).await(Messages.MatchBingoRes.class);
        ensure(client2Match.roomId().equals(client1Match.roomId()));
        ensure(client2Match.state().status().equals("Running"));

        trace("await client1 player joined notify");
        Messages.PlayerJoinedNotify join = client1.await(client1SawClient2Join).payload();
        ensure(join.actorId().equals(client2Auth.actorId()));
        ensure(client2.receivedCount(SampleNames.PlayerJoinedPacket) == 0);
        trace("await client1 game started notify");
        ensure(client1.await(client1Started).payload().state().status().equals("Running"));
        trace("await client2 game started notify");
        ensure(client2.await(client2Started).payload().state().status().equals("Running"));

        Messages.SubmitBingoCardRes client2Card = client2
            .request(new Messages.SubmitBingoCardReq(client2Match.roomId(), BingoClientCards.Player2))
            .await(Messages.SubmitBingoCardRes.class);
        ensure(client2Card.state().status().equals("Running"));

        var client1Ended =
            client1.waitFor(SampleNames.GameEndedPacket).submit(Messages.BingoGameEndedNotify.class);
        var client2Ended =
            client2.waitFor(SampleNames.GameEndedPacket).submit(Messages.BingoGameEndedNotify.class);
        var client1NextDraw =
            client1.waitFor(SampleNames.NumberDrawnPacket).submit(Messages.BingoNumberDrawnNotify.class);
        var client2NextDraw =
            client2.waitFor(SampleNames.NumberDrawnPacket).submit(Messages.BingoNumberDrawnNotify.class);

        Messages.SubmitBingoCardRes client1Card = client1
            .request(new Messages.SubmitBingoCardReq(client1Match.roomId(), BingoClientCards.Player1))
            .await(Messages.SubmitBingoCardRes.class);
        ensure(client1Card.state().status().equals("Running"));

        List<Messages.BingoNumberDrawnNotify> drawnNumbers = new ArrayList<>();
        for (int drawSeq = 1; drawSeq <= 15; drawSeq++) {
            Messages.BingoNumberDrawnNotify client1Drawn =
                client1.await(client1NextDraw).payload();
            Messages.BingoNumberDrawnNotify client2Drawn =
                client2.await(client2NextDraw).payload();
            drawnNumbers.add(client1Drawn);
            ensure(client1Drawn.drawSeq() == drawSeq);
            ensure(client2Drawn.drawSeq() == drawSeq);
            ensure(client2Drawn.number() == client1Drawn.number());

            if (client1Drawn.state().status().equals("Finished")) {
                break;
            }
            client1NextDraw =
                client1.waitFor(SampleNames.NumberDrawnPacket).submit(Messages.BingoNumberDrawnNotify.class);
            client2NextDraw =
                client2.waitFor(SampleNames.NumberDrawnPacket).submit(Messages.BingoNumberDrawnNotify.class);
        }
        ensure(!drawnNumbers.isEmpty());
        ensure(drawnNumbers.getLast().state().status().equals("Finished"));

        Messages.BingoRoomState client1Result = client1.await(client1Ended).payload().state();
        Messages.BingoRoomState client2Result = client2.await(client2Ended).payload().state();
        ensure(client1Result.status().equals("Finished"));
        ensure(client2Result.status().equals("Finished"));
        ensure(client2Result.drawnNumbers().equals(client1Result.drawnNumbers()));
        ensure(client2Result.winners().equals(client1Result.winners()));
        ensure(client2Result.players().stream().map(Messages.BingoPlayerState::actorId).toList()
                .equals(client1Result.players().stream().map(Messages.BingoPlayerState::actorId).toList()));
        ensure(client1Result.drawnNumbers().equals(drawnNumbers.stream()
                .map(Messages.BingoNumberDrawnNotify::number)
                .toList()));
        ensure(client1Result.winners().equals(List.of(client1Auth.actorId())));
        ensure(client1Result.players().stream().allMatch(player -> player.card().size() == 9));
        ensure(client1Result.players().stream().allMatch(player -> Boolean.TRUE.equals(player.marks().get(4))));
    }

	    private static void ensure(boolean condition) {
	        if (!condition) {
	            throw new IllegalStateException("Ensure failed");
	        }
	    }

	    private static void trace(String message) {
	        System.out.println("bingo scenario: " + message);
	    }

    private static final class BingoClientCards {
        private static final List<Integer> Player1 = List.of(1, 2, 3, 4, 0, 6, 7, 8, 9);
        private static final List<Integer> Player2 = List.of(10, 11, 12, 13, 0, 14, 4, 5, 6);
    }
}
