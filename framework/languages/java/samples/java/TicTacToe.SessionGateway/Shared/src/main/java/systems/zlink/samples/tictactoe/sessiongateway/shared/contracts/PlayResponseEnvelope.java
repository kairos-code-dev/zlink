package systems.zlink.samples.tictactoe.sessiongateway.shared.contracts;

import java.util.ArrayList;
import java.util.List;

public final class PlayResponseEnvelope {
    private PlayResponseEnvelope() {
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

    public record PlayResponse(String reply, List<Notification> notifications) {
    }

    public record Notification(String packetName, String recipientActorId, String payload) {
    }
}
