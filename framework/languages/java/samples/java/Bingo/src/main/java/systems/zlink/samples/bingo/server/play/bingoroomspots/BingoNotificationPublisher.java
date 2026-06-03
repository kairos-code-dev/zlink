package systems.zlink.samples.bingo.server.play.bingoroomspots;

import java.util.List;
import java.util.concurrent.CompletionStage;
import systems.zlink.samples.bingo.server.play.actors.PlayerActor;

public final class BingoNotificationPublisher {
    public CompletionStage<Void> publishWinnerAsync(
        PlayerActor actor,
        List<String> winners,
        String roomId) {
        return actor.context()
            .boundSession()
            .send(new BingoRoomModels.BingoWinner(String.join(",", winners)))
            .packetName("BingoWinner")
            .metadata("roomId", roomId)
            .submitAsync();
    }
}
