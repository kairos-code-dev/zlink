package systems.zlink.framework.spring;

import java.util.Optional;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorManager;

final class ZLinkFrameworkActorManagerBean implements ZLinkActorManager {
    private final ZLinkFrameworkLifecycle lifecycle;

    ZLinkFrameworkActorManagerBean(ZLinkFrameworkLifecycle lifecycle) {
        this.lifecycle = lifecycle;
    }

    @Override
    public CompletionStage<ZLinkActor> createAsync(String actorId, String actorType) {
        return lifecycle.actorManager().createAsync(actorId, actorType);
    }

    @Override
    public CompletionStage<Optional<ZLinkActor>> findAsync(String actorId) {
        return lifecycle.actorManager().findAsync(actorId);
    }

    @Override
    public CompletionStage<ZLinkActor> getOrCreateAsync(String actorId, String actorType) {
        return lifecycle.actorManager().getOrCreateAsync(actorId, actorType);
    }
}
