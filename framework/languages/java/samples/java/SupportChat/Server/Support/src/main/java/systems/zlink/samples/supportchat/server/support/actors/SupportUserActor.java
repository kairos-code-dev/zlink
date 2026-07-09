package systems.zlink.samples.supportchat.server.support.actors;

import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;

public final class SupportUserActor implements ZLinkActor {
    private final String actorId;
    private final ZLinkActorContext context;
    private String displayName;
    private String role = "";

    public SupportUserActor(String actorId, ZLinkActorContext context) {
        this.actorId = actorId;
        this.context = context;
        this.displayName = actorId;
    }

    @Override
    public String actorId() {
        return actorId;
    }

    @Override
    public ZLinkActorContext context() {
        return context;
    }

    public String displayName() {
        return displayName;
    }

    public String role() {
        return role;
    }

    public void setIdentity(String displayName, String role) {
        this.displayName = displayName;
        this.role = role;
    }

    public void push(Object message) {
        context.boundSession()
            .send(message)
            .submit();
    }
}
