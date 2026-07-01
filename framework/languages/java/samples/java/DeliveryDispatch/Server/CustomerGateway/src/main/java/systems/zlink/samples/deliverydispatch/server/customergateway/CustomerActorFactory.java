package systems.zlink.samples.deliverydispatch.server.customergateway;

import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorFactory;

public final class CustomerActorFactory implements ZLinkActorFactory {
    @Override
    public ZLinkActor create(String actorId, ZLinkActorContext context) {
        return new CustomerActor(actorId, context);
    }
}
