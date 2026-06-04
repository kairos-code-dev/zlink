package systems.zlink.samples.bingo.client;

import java.util.ArrayList;
import java.util.List;

public final class BingoNotificationInbox {
    private final List<String> events = new ArrayList<>();

    public void add(String event) {
        events.add(event);
    }

    public List<String> events() {
        return List.copyOf(events);
    }
}
