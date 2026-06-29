package systems.zlink.e2e.yielddispatch.shared;

import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;

public final class YieldActor implements ZLinkActor {
    private final String actorId;
    private final ZLinkActorContext context;

    public YieldActor(String actorId, ZLinkActorContext context) {
        this.actorId = actorId;
        this.context = context;
    }

    @Override
    public String actorId() {
        return actorId;
    }

    @Override
    public ZLinkActorContext context() {
        return context;
    }
}
