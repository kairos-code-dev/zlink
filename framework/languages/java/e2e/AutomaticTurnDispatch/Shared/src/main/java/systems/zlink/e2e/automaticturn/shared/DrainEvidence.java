package systems.zlink.e2e.automaticturn.shared;

import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;
import systems.zlink.framework.monitoring.ZLinkFrameworkRuntimeEvent;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventHandler;
import java.time.Instant;

public final class DrainEvidence implements ZLinkRuntimeEventHandler<ZLinkFrameworkRuntimeEvent> {
    public record Event(String state, Instant timestamp) {}

    private final List<Event> events = new CopyOnWriteArrayList<>();

    @Override
    public void handle(ZLinkFrameworkRuntimeEvent event) {
        events.add(new Event(event.runtime().state().name(), event.timestamp()));
    }

    public List<Event> events() {
        return List.copyOf(events);
    }

    public void observeServing() {
        if (events.stream().noneMatch(event -> event.state().equals("SERVING"))) {
            events.add(new Event("SERVING", Instant.now()));
        }
    }
}
