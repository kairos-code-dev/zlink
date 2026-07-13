package systems.zlink.e2e.automaticturn.shared;

import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;
import systems.zlink.framework.monitoring.ZLinkDrainEvent;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventHandler;
import systems.zlink.framework.monitoring.ZLinkDrainState;
import java.time.Instant;

public final class DrainEvidence implements ZLinkRuntimeEventHandler<ZLinkDrainEvent> {
    private final List<ZLinkDrainEvent> events = new CopyOnWriteArrayList<>();

    @Override
    public void handle(ZLinkDrainEvent event) {
        events.add(event);
    }

    public List<ZLinkDrainEvent> events() {
        return List.copyOf(events);
    }

    public void observeServing() {
        if (events.stream().noneMatch(event -> event.state() == ZLinkDrainState.SERVING)) {
            events.add(new ZLinkDrainEvent(ZLinkDrainState.SERVING, Instant.now()));
        }
    }
}
