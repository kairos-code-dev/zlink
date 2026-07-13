package systems.zlink.samples.deliverydispatch.server.customergateway;

import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;

public final class CustomerActor implements ZLinkActor {
    private final String actorId;
    private final ZLinkActorContext context;

    public CustomerActor(String actorId, ZLinkActorContext context) {
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

    public void push(Object message) {
        context.boundSession()
            .send(message)
            .submit();
    }
}
