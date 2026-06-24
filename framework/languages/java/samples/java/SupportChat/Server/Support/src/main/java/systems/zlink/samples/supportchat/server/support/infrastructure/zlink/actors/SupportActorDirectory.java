package systems.zlink.samples.supportchat.server.support.infrastructure.zlink.actors;

import java.util.HashMap;
import java.util.Map;
import systems.zlink.framework.actors.ZLinkActorRef;

public final class SupportActorDirectory {
    public record Entry(SupportUserActor actor, ZLinkActorRef ref, String displayName, String role) {
    }

    private final Object gate = new Object();
    private final Map<String, Entry> actors = new HashMap<>();

    public void addOrUpdate(SupportUserActor actor, ZLinkActorRef ref) {
        synchronized (gate) {
            actors.put(ref.actorId(), new Entry(actor, ref, actor.displayName(), actor.role()));
        }
    }

    public Entry get(String actorId) {
        synchronized (gate) {
            Entry entry = actors.get(actorId);
            if (entry == null) {
                throw new IllegalStateException("Support actor is not available. actor=" + actorId);
            }
            return entry;
        }
    }
}
