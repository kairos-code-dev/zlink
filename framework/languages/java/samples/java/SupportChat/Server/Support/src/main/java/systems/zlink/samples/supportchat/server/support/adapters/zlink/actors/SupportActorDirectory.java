package systems.zlink.samples.supportchat.server.support.adapters.zlink.actors;

import java.util.HashMap;
import java.util.Map;

public final class SupportActorDirectory {
    private final Object gate = new Object();
    private final Map<String, SupportUserActor> actors = new HashMap<>();

    public void addOrUpdate(SupportUserActor actor) {
        synchronized (gate) {
            actors.put(actor.actorId(), actor);
        }
    }

    public SupportUserActor get(String actorId) {
        synchronized (gate) {
            SupportUserActor actor = actors.get(actorId);
            if (actor == null) {
                throw new IllegalStateException("Support actor is not available. actor=" + actorId);
            }
            return actor;
        }
    }
}
