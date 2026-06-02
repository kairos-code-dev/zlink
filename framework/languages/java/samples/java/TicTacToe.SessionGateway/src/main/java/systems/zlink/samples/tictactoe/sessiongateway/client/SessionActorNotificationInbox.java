package systems.zlink.samples.tictactoe.sessiongateway.client;

import java.util.ArrayList;
import java.util.List;

public final class SessionActorNotificationInbox {
    private final List<String> events = new ArrayList<>();

    public void addAll(List<String> values) {
        events.addAll(values);
    }

    public List<String> events() {
        return List.copyOf(events);
    }
}
