package systems.zlink.e2e.spotservice;

import java.util.List;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;

public final class ScenarioActor implements ZLinkActor {
    private final String actorId;
    private final ZLinkActorContext context;
    private Contracts.ActorProfile profile = new Contracts.ActorProfile("", 0, List.of());
    private int sequence;

    public ScenarioActor(
        String actorId,
        ZLinkActorContext context) {
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

    public int nextSequence() {
        sequence++;
        return sequence;
    }

    public void applyProfile(Contracts.ActorProfile next) {
        profile = next;
    }
}
