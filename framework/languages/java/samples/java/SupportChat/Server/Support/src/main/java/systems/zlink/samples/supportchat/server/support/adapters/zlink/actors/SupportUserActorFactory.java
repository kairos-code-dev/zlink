package systems.zlink.samples.supportchat.server.support.adapters.zlink.actors;

import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorFactory;

public final class SupportUserActorFactory implements ZLinkActorFactory {
    @Override
    public ZLinkActor create(String actorId, ZLinkActorContext context) {
        return new SupportUserActor(actorId, context);
    }
}
