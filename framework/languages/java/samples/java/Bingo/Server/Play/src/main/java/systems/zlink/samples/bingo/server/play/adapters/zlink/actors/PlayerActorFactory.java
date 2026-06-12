package systems.zlink.samples.bingo.server.play.adapters.zlink.actors;

import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorFactory;

public final class PlayerActorFactory implements ZLinkActorFactory {
    @Override
    public ZLinkActor create(String actorId, ZLinkActorContext context) {
        return new PlayerActor(actorId, context);
    }
}
