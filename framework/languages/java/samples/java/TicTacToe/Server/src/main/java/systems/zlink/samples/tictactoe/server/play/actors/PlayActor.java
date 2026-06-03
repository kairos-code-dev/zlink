package systems.zlink.samples.tictactoe.server.play.actors;

import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;

public final class PlayActor implements ZLinkActor {
    private final String actorId;
    private final ZLinkActorContext context;

    public PlayActor(String actorId, ZLinkActorContext context) {
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
