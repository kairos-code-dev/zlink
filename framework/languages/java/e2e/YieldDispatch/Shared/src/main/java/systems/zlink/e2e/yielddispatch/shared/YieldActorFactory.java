package systems.zlink.e2e.yielddispatch.shared;

import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorFactory;

public final class YieldActorFactory implements ZLinkActorFactory {
    @Override
    public ZLinkActor create(String actorId, ZLinkActorContext context) {
        return new YieldActor(actorId, context);
    }
}
