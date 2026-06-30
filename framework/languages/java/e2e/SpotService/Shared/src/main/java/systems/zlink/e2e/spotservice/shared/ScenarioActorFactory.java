package systems.zlink.e2e.spotservice.shared;

import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorFactory;

public final class ScenarioActorFactory implements ZLinkActorFactory {
    @Override
    public ZLinkActor create(
        String actorId,
        ZLinkActorContext context) {
        return new ScenarioActor(actorId, context);
    }
}
