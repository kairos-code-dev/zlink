package systems.zlink.samples.bingo.server.play.actors;

import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;

public final class PlayerActor implements ZLinkActor {
    private final String actorId;
    private final ZLinkActorContext context;

    public PlayerActor(String actorId, ZLinkActorContext context) {
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
