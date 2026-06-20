package systems.zlink.samples.bingo.client;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletionStage;
import systems.zlink.samples.bingo.client.configuration.SampleNames;
import systems.zlink.samples.bingo.shared.contracts.Messages;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamMessage;

public final class BingoClientScenario {
    public void run(
        ZLinkStreamConnector client1,
        ZLinkStreamConnector client2,
        ZLinkStreamConnector observer) throws Exception {
        client1.connect().await();
        client2.connect().await();
        observer.connect().await();

        Messages.AuthenticateRes client1Auth =
            client1.request(new Messages.AuthenticateReq("player-1")).await(Messages.AuthenticateRes.class);
        ensure(client1Auth.actorId().equals("player-1"));
        ensure(!client1Auth.actorNodeRid().isBlank());

        Messages.MatchBingoRes client1Match =
            client1.request(new Messages.MatchBingoReq("two-player")).await(Messages.MatchBingoRes.class);
        ensure(client1Match.state().status().equals("WaitingForPlayers"));
        ensure(client1Match.state().hostActorId().equals(client1Auth.actorId()));
        ensure(client1Match.roomOwnerNodeRid().equals(client1Auth.actorNodeRid()));
        ensure(client1.receivedCount(SampleNames.PlayerJoinedPacket) == 0);

        Messages.AuthenticateRes observerAuth =
            observer.request(new Messages.AuthenticateReq("observer")).await(Messages.AuthenticateRes.class);
        ensure(observerAuth.actorId().equals("observer"));
        ensure(!observerAuth.actorNodeRid().equals(client1Match.roomOwnerNodeRid()));
        Messages.ObserveBingoEventsRes observed = observer
            .request(new Messages.ObserveBingoEventsReq(client1Match.roomId()))
            .await(Messages.ObserveBingoEventsRes.class);
        ensure(observed.subscribed());
        ensure(observed.observerNodeRid().equals(observerAuth.actorNodeRid()));
        ensure(!observed.observerNodeRid().equals(client1Match.roomOwnerNodeRid()));

        var client1SawClient2Join =
            client1.waitFor(SampleNames.PlayerJoinedPacket).submit(Messages.PlayerJoinedNotify.class);
        var client1Started =
            client1.waitFor(SampleNames.GameStartedPacket).submit(Messages.BingoGameStartedNotify.class);

        Messages.AuthenticateRes client2Auth =
            client2.request(new Messages.AuthenticateReq("player-2")).await(Messages.AuthenticateRes.class);
        ensure(client2Auth.actorId().equals("player-2"));
        ensure(!client2Auth.actorId().equals(client1Auth.actorId()));
        ensure(!client2Auth.actorNodeRid().equals(client1Auth.actorNodeRid()));
        var client2Started =
            client2.waitFor(SampleNames.GameStartedPacket).submit(Messages.BingoGameStartedNotify.class);

        Messages.MatchBingoRes client2Match =
            client2.request(new Messages.MatchBingoReq("two-player")).await(Messages.MatchBingoRes.class);
        ensure(client2Match.roomId().equals(client1Match.roomId()));
        ensure(client2Match.state().status().equals("Running"));
        ensure(client2Match.roomOwnerNodeRid().equals(client1Match.roomOwnerNodeRid()));
        ensure(!client2Auth.actorNodeRid().equals(client2Match.roomOwnerNodeRid()));

        Messages.PlayerJoinedNotify join = client1.await(client1SawClient2Join).payload();
        ensure(join.actorId().equals(client2Auth.actorId()));
        ensure(client2.receivedCount(SampleNames.PlayerJoinedPacket) == 0);
        ensure(client1.await(client1Started).payload().state().status().equals("Running"));
        ensure(client2.await(client2Started).payload().state().status().equals("Running"));

        Messages.SubmitBingoCardRes client2Card = client2
            .request(new Messages.SubmitBingoCardReq(client2Match.roomId(), BingoClientCards.Player2))
            .await(Messages.SubmitBingoCardRes.class);
        ensure(client2Card.state().status().equals("Running"));
        var rewardAnnounced = observer.waitFor(SampleNames.RewardAnnouncedPacket)
            .where(
                Messages.BingoRewardAnnouncedNotify.class,
                message -> message.payload().roomId().equals(client1Match.roomId()))
            .submit(Messages.BingoRewardAnnouncedNotify.class);

        var client1Ended =
            client1.waitFor(SampleNames.GameEndedPacket).submit(Messages.BingoGameEndedNotify.class);
        var client2Ended =
            client2.waitFor(SampleNames.GameEndedPacket).submit(Messages.BingoGameEndedNotify.class);
        List<CompletionStage<ZLinkStreamMessage<Messages.BingoNumberDrawnNotify>>> client1Draws =
            new ArrayList<>();
        List<CompletionStage<ZLinkStreamMessage<Messages.BingoNumberDrawnNotify>>> client2Draws =
            new ArrayList<>();
        for (int drawSeq = 1; drawSeq <= 15; drawSeq++) {
            int expectedDrawSeq = drawSeq;
            client1Draws.add(client1.waitFor(SampleNames.NumberDrawnPacket)
                .where(
                    Messages.BingoNumberDrawnNotify.class,
                    message -> message.payload().drawSeq() == expectedDrawSeq)
                .submit(Messages.BingoNumberDrawnNotify.class));
            client2Draws.add(client2.waitFor(SampleNames.NumberDrawnPacket)
                .where(
                    Messages.BingoNumberDrawnNotify.class,
                    message -> message.payload().drawSeq() == expectedDrawSeq)
                .submit(Messages.BingoNumberDrawnNotify.class));
        }

        Messages.SubmitBingoCardRes client1Card = client1
            .request(new Messages.SubmitBingoCardReq(client1Match.roomId(), BingoClientCards.Player1))
            .await(Messages.SubmitBingoCardRes.class);
        ensure(client1Card.state().status().equals("Running"));

        List<Messages.BingoNumberDrawnNotify> drawnNumbers = new ArrayList<>();
        for (int drawSeq = 1; drawSeq <= 15; drawSeq++) {
            Messages.BingoNumberDrawnNotify client1Drawn =
                client1.await(client1Draws.get(drawSeq - 1)).payload();
            Messages.BingoNumberDrawnNotify client2Drawn =
                client2.await(client2Draws.get(drawSeq - 1)).payload();
            drawnNumbers.add(client1Drawn);
            ensure(client1Drawn.drawSeq() == drawSeq);
            ensure(client2Drawn.drawSeq() == drawSeq);
            ensure(client2Drawn.number() == client1Drawn.number());

            if (client1Drawn.state().status().equals("Finished")) {
                break;
            }
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

        Messages.BingoRewardAnnouncedNotify reward = observer.await(rewardAnnounced).payload();
        ensure(reward.actorId().equals(client1Auth.actorId()));
        ensure(reward.itemId().equals("rare-golden-dauber"));
        ensure(reward.itemName().equals("Golden Dauber"));
        ensure(reward.rarity().equals("Legendary"));
        ensure(reward.receivingSpotNodeRid().equals(observed.observerNodeRid()));
        ensure(!reward.receivingSpotNodeRid().equals(client1Match.roomOwnerNodeRid()));

        Messages.StopObservingBingoEventsRes stopped = observer
            .request(new Messages.StopObservingBingoEventsReq(client1Match.roomId()))
            .await(Messages.StopObservingBingoEventsRes.class);
        ensure(stopped.stopped());
        ensure(stopped.observerNodeRid().equals(observed.observerNodeRid()));
    }

    private static void ensure(boolean condition) {
        if (!condition) {
            throw new IllegalStateException("Ensure failed");
        }
    }

    private static final class BingoClientCards {
        private static final List<Integer> Player1 = List.of(1, 2, 3, 4, 0, 6, 7, 8, 9);
        private static final List<Integer> Player2 = List.of(10, 11, 12, 13, 0, 14, 4, 5, 6);
    }
}
