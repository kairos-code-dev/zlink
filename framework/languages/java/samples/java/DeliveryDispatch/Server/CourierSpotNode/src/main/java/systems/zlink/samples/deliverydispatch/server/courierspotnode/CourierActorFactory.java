package systems.zlink.samples.deliverydispatch.server.courierspotnode;

import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorFactory;

public final class CourierActorFactory implements ZLinkActorFactory {
    @Override
    public ZLinkActor create(String actorId, ZLinkActorContext context) {
        return new CourierActor(actorId, context);
    }
}
