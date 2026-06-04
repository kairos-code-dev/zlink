package systems.zlink.samples.tictactoe.sessiongateway.server.play.gamespots;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.actors.ZLinkBoundSession;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleNames;

public final class GameNotificationPublisher {
    private GameNotificationPublisher() {
    }

    public static String encode(TicTacToeGameSpot.JoinResult result) {
        return encode(result.encode(), result.events());
    }

    public static String encode(TicTacToeGameSpot.MoveResult result) {
        return encode(result.encode(), result.events());
    }

    public static PlayResponse decode(String value) {
        String[] lines = value.split("\\n", -1);
        List<Notification> notifications = new ArrayList<>();
        for (int index = 1; index < lines.length; index++) {
            if (lines[index].isBlank()) {
                continue;
            }
            String[] parts = lines[index].split("\\|", 3);
            notifications.add(new Notification(parts[0], parts[1], parts[2]));
        }
        return new PlayResponse(lines[0], notifications);
    }

    private static String encode(String reply, List<TicTacToeGameSpot.GameEvent> events) {
        List<String> lines = new ArrayList<>();
        lines.add(reply);
        for (TicTacToeGameSpot.GameEvent event : events) {
            lines.add(packetName(event) + "|" + event.recipientActorId() + "|" + payload(event));
        }
        return String.join("\n", lines);
    }

    private static String packetName(TicTacToeGameSpot.GameEvent event) {
        return switch (event.kind()) {
            case "OpponentJoined" -> SampleNames.OpponentJoinedPacket;
            case "GameEnded" -> SampleNames.GameEndedPacket;
            default -> SampleNames.TurnChangedPacket;
        };
    }

    private static String payload(TicTacToeGameSpot.GameEvent event) {
        return event.state().encode();
    }

    public CompletionStage<Void> publishAsync(ZLinkBoundSession session, String state) {
        return session.send(state)
            .packetName("GameStateChanged")
            .submitAsync();
    }

    public record PlayResponse(String reply, List<Notification> notifications) {
    }

    public record Notification(String packetName, String recipientActorId, String payload) {
    }
}
